#include <cassert>
#include <thread>

#include <ch_frb_io.hpp>
#include <ch_frb_io_internals.hpp>

#include "ch_frb_l1.hpp"
#include "simulate-l0.hpp"

using namespace std;
using namespace ch_frb_l1;

using ch_frb_io::lexical_cast;

static void usage()
{
    cerr << "Usage: ch-frb-simulate-l0 <l0_params.yaml> <num_seconds OR target_gbps if msgpack files given> [msgpack-chunk-files ...]\n";
    exit(2);
}


int main(int argc, char **argv)
{
    if (argc < 3)
	usage();

    string filename = argv[1];
    double num_seconds = lexical_cast<double> (argv[2]);
    double gbps = 0.0;
    
    vector<string> datafiles;
    for (int a=3; a<argc; a++) {
        datafiles.push_back(string(argv[a]));
    }

    if (datafiles.size())
        gbps = num_seconds;
    else
        if (num_seconds <= 0.0)
            usage();

    l0_params p(filename);
    p.write(cout);

    int nthreads = p.nthreads_tot;

    vector<shared_ptr<ch_frb_io::intensity_network_ostream>> streams(nthreads);
    vector<std::thread> threads(nthreads);

    for (int ithread = 0; ithread < p.nthreads_tot; ithread++) {
	streams[ithread] = p.make_ostream(ithread, gbps);
	streams[ithread]->print_status();
    }

    if (datafiles.size() == 0) {
        for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
            threads[ithread] = std::thread(sim_thread_main, streams[ithread], num_seconds);
    } else {
        for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
            threads[ithread] = std::thread(data_thread_main, streams[ithread], datafiles);
    }
    
    for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
	threads[ithread].join();

    // We postpone the calls to intensity_network_ostream::end_stream() until all sim_threads
    // have finished (see explanation above).
    for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
	streams[ithread]->end_stream(true);  // "true" joins network thread

    for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
	streams[ithread]->print_status();

    return 0;
}
