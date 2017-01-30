// This is a toy program which receives a packet stream and doesn't do anything with it!
// The sender will usually be the counterpart toy program 'ch-frb-l0-simulate'.
//
// This is a placeholder for the full L1 process.  Currently data goes through the network and
// assembler threads, but doesn't get RFI-cleaned or dedispersed.  This will be expanded soon!
// In the meantime, it's still useful for timing the network receive code.

#include <cassert>
#include <iostream>
#include <sstream>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ch_frb_io.hpp>
#include <l1-rpc.hpp>

using namespace std;
using namespace ch_frb_io;

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
        cout << "Decoded beam " << chunk->beam_id << ", chunk " << chunk->ichunk << endl;
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
    cerr << "usage: ch-frb-l1 [-r]\n" <<
        "      [-u <L1 udp-port>]\n" <<
        "      [-a <RPC address>] [-p <RPC port number>]\n" <<
        "      [-b <beam id> (may be repeated)]\n" <<
        "      [-h for help]" <<
        "   -r uses reference kernels instead of avx2 kernels\n" <<
        "  Addresses are like:\n" <<
        "   -a tcp://127.0.0.1:5555" << endl;
    exit(2);
}


int main(int argc, char **argv) {

    ch_frb_io::intensity_network_stream::initializer ini_params;

#ifdef __AVX2__
    ini_params.mandate_fast_kernels = true;
#else
    cerr << "Warning: this machine does not appear to have the AVX2 instruction set, fast kernels will be disabled\n";
#endif

    string rpc_port = "";
    int rpc_portnum = 0;

    int c;
    while ((c = getopt(argc, argv, "a:p:b:u:wh")) != -1) {
        switch (c) {
        case 'a':
            rpc_port = string(optarg);
            break;

        case 'p':
            rpc_portnum = atoi(optarg);
            break;

        case 'b':
            {
                int beam = atoi(optarg);
                ini_params.beam_ids.push_back(beam);
                break;
            }

        case 'u':
            {
                int udpport = atoi(optarg);
                ini_params.udp_port = udpport;
                break;
            }

        case 'r':
            ini_params.mandate_fast_kernels = false;
            ini_params.mandate_reference_kernels = true;
            break;

        case 'h':
        case '?':
        default:
            usage();
            return 0;
        }
    }
    argc -= optind;
    argv += optind;

    if (ini_params.beam_ids.size() == 0) {
        ini_params.beam_ids = { 0, 1, 2, 3, 4, 5, 6, 7 };
    }

    // Make input stream object
    auto stream = ch_frb_io::intensity_network_stream::make(ini_params);

    // Spawn one processing thread per beam
    pthread_t processing_threads[8];
    for (int ibeam = 0; ibeam < 8; ibeam++)
	spawn_processing_thread(processing_threads[ibeam], stream, ibeam);

    if ((rpc_port.length() == 0) && (rpc_portnum == 0))
        rpc_port = "tcp://127.0.0.1:5555";
    else if (rpc_portnum)
        rpc_port = "tcp://127.0.0.1:" + to_string(rpc_portnum);

    cout << "Starting RPC server on " << rpc_port << endl;
    bool rpc_exited = false;
    pthread_t* rpc_thread = l1_rpc_server_start(stream, rpc_port, &rpc_exited);

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
