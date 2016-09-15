// A placeholder for the full L1 process.  Currently data goes through the complete network front end,
// but doesn't get RFI-cleaned or dedispersed.

#include "ch_frb_io_internals.hpp"

using namespace std;


static void *consumer_thread_main(void *opaque_arg)
{
    shared_ptr<ch_frb_io::intensity_beam_assembler> *context = reinterpret_cast<shared_ptr<ch_frb_io::intensity_beam_assembler> *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_beam_assembler> assembler = *context;
    delete context;

    for (;;) {
	shared_ptr<ch_frb_io::assembled_chunk> chunk;
	bool alive = assembler->get_assembled_chunk(chunk);
	if (!alive)
	    break;
    }

    return NULL;
}


static void spawn_consumer_thread(pthread_t &thread, const shared_ptr<ch_frb_io::intensity_beam_assembler> &assembler)
{
    shared_ptr<ch_frb_io::intensity_beam_assembler> *context = new shared_ptr<ch_frb_io::intensity_beam_assembler> (assembler);

    int err = pthread_create(&thread, NULL, consumer_thread_main, &context);
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
	auto assembler = ch_frb_io::intensity_beam_assembler::make(ibeam, false);   // drops_allowed = false
	spawn_consumer_thread(consumer_threads[ibeam], assembler);
	assemblers.push_back(assembler);
    }

    shared_ptr<ch_frb_io::intensity_network_stream> stream = ch_frb_io::intensity_network_stream::make(assemblers, ch_frb_io::constants::default_udp_port);
    stream->start_stream();
    stream->join_all_threads();

    return 0;
}
