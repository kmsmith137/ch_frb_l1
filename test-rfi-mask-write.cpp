#include <cassert>
#include <thread>

#include <ch_frb_io.hpp>
#include <ch_frb_io_internals.hpp>

#include "ch_frb_l1.hpp"
#include "simulate-l0.hpp"

// msgpack
#include <msgpack.hpp>
#include <sstream>
#include <rpc.hpp>

using namespace std;
using namespace ch_frb_io;
using namespace ch_frb_l1;

using ch_frb_io::lexical_cast;

static void usage()
{
    cerr << "Usage: ch-frb-simulate-l0 <l0_params.yaml>\n";
    exit(2);
}


int main(int argc, char **argv)
{
    if (argc < 2)
	usage();

    string filename = argv[1];
    l0_params p(filename);
    p.write(cout);

    assert(p.nthreads_tot == 1);

    /*
     const char* destination = "tcp://127.0.0.1:5555";
     //  Prepare our context and socket
     zmq::context_t context(1);
     zmq::socket_t socket(context, ZMQ_DEALER);
     cout << "Connecting to L1 RPC server at..." << destination << endl;
     socket.connect(destination);
     msgpack::sbuffer buffer;
     Rpc_Request rpc;
     rpc.function = "get_statistics";
     rpc.token = 42;
     */
    
    ch_frb_io::assembled_chunk::initializer ini;
    ini.beam_id = 0;
    ini.nrfifreq = 1024;
    ini.nupfreq = p.nupfreq;
    ini.nt_per_packet = p.nt_per_packet;
    ini.fpga_counts_per_sample = p.fpga_counts_per_sample;
    
    vector<shared_ptr<ch_frb_io::assembled_chunk> > chunks;

    std::random_device rd;
    unsigned int seed = rd();
    std::ranlux48_base rando(seed);
    float mean = 128;
    float stddev = 20;
    // uniform samples between [-2, +2] sigma
    float r0 = mean - 2.*stddev;
    float scale = 4.*stddev / (rando.max() - rando.min());

    int nfreq = constants::nfreq_coarse_tot * ini.nupfreq;
    int nt = constants::nt_per_assembled_chunk;
    
    for (int j=0; j<10; j++) {
        shared_ptr<assembled_chunk> chunk = assembled_chunk::make(ini);
        for (int i=0; i<chunk->ndata; i++) {
            chunk->data[i] = (uint8_t)(r0 + scale * (float)rando());
        }

        // data: 2d array of shape (constants::nfreq_coarse * nupfreq, constants::nt_per_assembled_chunk)
        // Create a big spike in the time direction
        for (int f=0; f<nfreq; f++) {
            for (int i=0; i<100; i++) {
                chunk->data[f * nt + j*50 + i] = 250;
            }
        }

        chunks.push_back(chunk);
    }
    
    int nthreads = p.nthreads_tot;
    vector<shared_ptr<ch_frb_io::intensity_network_ostream>> streams(nthreads);
    vector<std::thread> threads(nthreads);

    double gbps = 0.;
    
    for (int ithread = 0; ithread < p.nthreads_tot; ithread++) {
	streams[ithread] = p.make_ostream(ithread, gbps);
	streams[ithread]->print_status();
    }

    cout << "Sending data..." << endl;
    for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
        threads[ithread] = std::thread(data_thread_main, streams[ithread], chunks);

    for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
	threads[ithread].join();

    // this happens relatively quickly; end_stream() waits for the send to finish.
    cout << "Joined threads" << endl;
    
    // We postpone the calls to intensity_network_ostream::end_stream() until all sim_threads
    // have finished (see explanation above).
    for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
	streams[ithread]->end_stream(true);  // "true" joins network thread

    cout << "Sent end_stream" << endl;

    for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
	streams[ithread]->print_status();

    return 0;
}
