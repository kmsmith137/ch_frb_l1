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

void sim_thread_main(shared_ptr<l0_params> l0, int istream, double num_seconds) {
    l0->send_noise(istream, num_seconds);
}

void chunk_files_thread_main(shared_ptr<l0_params> l0, int istream,
                             vector<string> datafiles) {
    l0->send_chunk_files(istream, datafiles);
}

int main(int argc, char **argv)
{
    if (argc < 3)
	usage();

    string filename = argv[1];
    double num_seconds = lexical_cast<double> (argv[2]);
    double gbps = 0.0;

    // HACK
    bool send_eos=false;
    //bool send_eos=true;
    
    vector<string> datafiles;
    for (int a=3; a<argc; a++) {
        datafiles.push_back(string(argv[a]));
    }

    if (datafiles.size())
        gbps = num_seconds;
    else
        if (num_seconds <= 0.0)
            usage();

    shared_ptr<l0_params> p = make_shared<l0_params>(filename, gbps, send_eos);
    p->write(cout);

    int nthreads = p->nthreads_tot;

    vector<std::thread> threads(nthreads);

    if (datafiles.size() == 0) {
        for (int ithread = 0; ithread < p->nthreads_tot; ithread++)
            threads[ithread] = std::thread(sim_thread_main, p, ithread, num_seconds);
    } else {
        for (int ithread = 0; ithread < p->nthreads_tot; ithread++)
            threads[ithread] = std::thread(chunk_files_thread_main, p, ithread, datafiles);
    }
    
    for (int ithread = 0; ithread < p->nthreads_tot; ithread++)
	threads[ithread].join();

    p->end_streams();
    
    return 0;
}
