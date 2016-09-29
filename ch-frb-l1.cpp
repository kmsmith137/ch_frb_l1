// This is a toy program which receives a packet stream and doesn't do anything with it!
// The sender will usually be the counterpart toy program 'ch-frb-l0-simulate'.
//
// This is a placeholder for the full L1 process.  Currently data goes through the network and
// assembler threads, but doesn't get RFI-cleaned or dedispersed.  This will be expanded soon!
// In the meantime, it's still useful for timing the network receive code.

#include <cassert>
#include <iostream>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ch_frb_io.hpp>

#if defined(__AVX2__)
const static bool HAVE_AVX2 = true;
#else
#warning "This machine does not have the AVX2 instruction set."
const static bool HAVE_AVX2 = false;
#endif

using namespace std;


// -------------------------------------------------------------------------------------------------
//
// Processing threads: we spawn one of these per beam, to process the real-time data.
// Currently this is a placeholder which reads the data and throws it away!


struct processing_thread_context {
    shared_ptr<ch_frb_io::intensity_network_stream> stream;
    int ithread = -1;
};


static void *processing_thread_main(void *opaque_arg)
{
    const int nupfreq = 16;
    const int nalloc = ch_frb_io::constants::nfreq_coarse_tot * nupfreq * ch_frb_io::constants::nt_per_assembled_chunk;

    std::vector<float> intensity(nalloc, 0.0);
    std::vector<float> weights(nalloc, 0.0);

    processing_thread_context *context = reinterpret_cast<processing_thread_context *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    int ithread = context->ithread;
    delete context;

    for (;;) {
	// Get assembled data from netwrok
	auto chunk = stream->get_assembled_chunk(ithread);
	if (!chunk)
	    break;  // End-of-stream reached

	assert(chunk->nupfreq == nupfreq);

	// We call assembled_chunk::decode(), which extracts the data from its low-level 8-bit
	// representation to a floating-point array, but our processing currently stops there!
	chunk->decode(&intensity[0], &weights[0], ch_frb_io::constants::nt_per_assembled_chunk);
    }

    return NULL;
}


static void spawn_processing_thread(pthread_t &thread, const shared_ptr<ch_frb_io::intensity_network_stream> &stream, int ithread)
{
    processing_thread_context *context = new processing_thread_context;
    context->stream = stream;
    context->ithread = ithread;

    int err = pthread_create(&thread, NULL, processing_thread_main, context);
    if (err)
	throw runtime_error(string("pthread_create() failed to create processing thread: ") + strerror(errno));

    // no "delete context"!
}


static void usage()
{
    cerr << "usage: ch-frb-l1 [-r]\n"
	 << "   -r uses reference kernels instead of avx2 kernels\n";
    exit(2);
}


int main(int argc, char **argv)
{
    ch_frb_io::intensity_network_stream::initializer ini_params;
    ini_params.beam_ids = { 0, 1, 2, 3, 4, 5, 6, 7 };
    ini_params.mandate_fast_kernels = HAVE_AVX2;

    for (int i = 1; i < argc; i++) {
	if (strcmp(argv[i], "-r"))
	    usage();
	ini_params.mandate_fast_kernels = false;
	ini_params.mandate_reference_kernels = true;
    }

    // Make input stream object
    auto stream = ch_frb_io::intensity_network_stream::make(ini_params);

    // Spawn one processing thread per beam
    pthread_t processing_threads[8];
    for (int ibeam = 0; ibeam < 8; ibeam++)
	spawn_processing_thread(processing_threads[ibeam], stream, ibeam);

    // Start listening for packets.
    stream->start_stream();

    // This will block until the stream ends, and the network and assembler threads exit.
    // (The way this works is that the sender sends a special "end-of-stream" packet, to
    // indicate there is no more data, which initiates the stream shutdown process on the
    // receive side.)
    stream->join_threads();

    // Join processing threads
    for (int ibeam = 0; ibeam < 8; ibeam++) {
	int err = pthread_join(processing_threads[ibeam], NULL);
	if (err)
	    throw runtime_error("ch-frb-l1: pthread_join() failed [processing thread]");
    }

    return 0;
}
