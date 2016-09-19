// A placeholder for the full L1 process.  Currently data goes through the complete network front end,
// but doesn't get RFI-cleaned or dedispersed.

#include <cassert>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <ch_frb_io.hpp>

using namespace std;


static void *consumer_thread_main(void *opaque_arg)
{
    const int nupfreq = 16;
    const int nalloc = ch_frb_io::constants::nfreq_coarse * nupfreq * ch_frb_io::constants::nt_per_assembled_chunk;

    std::vector<float> intensity(nalloc, 0.0);
    std::vector<float> weights(nalloc, 0.0);

    shared_ptr<ch_frb_io::intensity_beam_assembler> *context = reinterpret_cast<shared_ptr<ch_frb_io::intensity_beam_assembler> *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_beam_assembler> assembler = *context;
    delete context;

    for (;;) {
	shared_ptr<ch_frb_io::assembled_chunk> chunk;
	bool alive = assembler->get_assembled_chunk(chunk);
	if (!alive)
	    break;

	assert(chunk->nupfreq == nupfreq);
	chunk->decode(&intensity[0], &weights[0], ch_frb_io::constants::nt_per_assembled_chunk);
    }

    return NULL;
}


static void spawn_consumer_thread(pthread_t &thread, const shared_ptr<ch_frb_io::intensity_beam_assembler> &assembler)
{
    shared_ptr<ch_frb_io::intensity_beam_assembler> *context = new shared_ptr<ch_frb_io::intensity_beam_assembler> (assembler);

    int err = pthread_create(&thread, NULL, consumer_thread_main, context);
    if (err)
	throw runtime_error(string("pthread_create() failed to create consumer thread: ") + strerror(errno));

    // no "delete context"!
}


int main(int argc, char **argv)
{
    const int nbeams = 8;

    vector<shared_ptr<ch_frb_io::intensity_beam_assembler> > assemblers(8);
    pthread_t consumer_threads[nbeams];

    for (int ibeam = 0; ibeam < nbeams; ibeam++) {
	assemblers[ibeam] = make_shared<ch_frb_io::intensity_beam_assembler> (ibeam, false);   // drops_allowed = false
	spawn_consumer_thread(consumer_threads[ibeam], assemblers[ibeam]);
    }

    shared_ptr<ch_frb_io::intensity_network_stream> stream = ch_frb_io::intensity_network_stream::make(assemblers, ch_frb_io::constants::default_udp_port);
    stream->start_stream();
    stream->join_network_thread();

    return 0;
}
