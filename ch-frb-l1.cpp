// A placeholder for the full L1 process.  Currently data goes through the complete network front end,
// but doesn't get RFI-cleaned or dedispersed.

#include <cassert>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <ch_frb_io.hpp>

using namespace std;


struct consumer_thread_context {
    shared_ptr<ch_frb_io::intensity_network_stream> stream;
    int ithread = -1;
};


static void *consumer_thread_main(void *opaque_arg)
{
    const int nupfreq = 16;
    const int nalloc = ch_frb_io::constants::nfreq_coarse * nupfreq * ch_frb_io::constants::nt_per_assembled_chunk;

    std::vector<float> intensity(nalloc, 0.0);
    std::vector<float> weights(nalloc, 0.0);

    consumer_thread_context *context = reinterpret_cast<consumer_thread_context *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    int ithread = context->ithread;
    delete context;

    for (;;) {
	auto chunk = stream->get_assembled_chunk(ithread);
	if (!chunk)
	    break;

	assert(chunk->nupfreq == nupfreq);
	chunk->decode(&intensity[0], &weights[0], ch_frb_io::constants::nt_per_assembled_chunk);
    }

    return NULL;
}


static void spawn_consumer_thread(pthread_t &thread, const shared_ptr<ch_frb_io::intensity_network_stream> &stream, int ithread)
{
    consumer_thread_context *context = new consumer_thread_context;
    context->stream = stream;
    context->ithread = ithread;

    int err = pthread_create(&thread, NULL, consumer_thread_main, context);
    if (err)
	throw runtime_error(string("pthread_create() failed to create consumer thread: ") + strerror(errno));

    // no "delete context"!
}


int main(int argc, char **argv)
{
    ch_frb_io::intensity_network_stream::initializer ini_params;
    ini_params.beam_ids = { 0, 1, 2, 3, 4, 5, 6, 7 };
    ini_params.mandate_fast_kernels = true;
    
    auto stream = ch_frb_io::intensity_network_stream::make(ini_params);

    pthread_t consumer_threads[8];
    for (int ibeam = 0; ibeam < 8; ibeam++)
	spawn_consumer_thread(consumer_threads[ibeam], stream, ibeam);

    stream->start_stream();
    stream->join_threads();

    return 0;
}
