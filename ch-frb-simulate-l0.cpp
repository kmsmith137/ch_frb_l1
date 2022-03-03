#include <cassert>
#include <thread>

#include <ch_frb_io.hpp>
#include <ch_frb_io_internals.hpp>

#include "ch_frb_l1.hpp"
#include "simulate-l0.hpp"
#include "CLI11.hpp"

using namespace std;
using namespace ch_frb_l1;

using ch_frb_io::lexical_cast;

static void usage()
{
    cerr << "Usage: ch-frb-simulate-l0 [-s: stream chunk files] <l0_params.yaml> <num_seconds OR target_gbps if msgpack files given> [msgpack-chunk-files ...]\n";
    exit(2);
}

void sim_thread_main(shared_ptr<l0_params> l0, int istream, double num_seconds) {
    l0->send_noise(istream, num_seconds);
}

void chunk_files_thread_main(shared_ptr<l0_params> l0, int istream,
                             vector<string> datafiles, bool stream) {
    if (stream)
        l0->stream_chunk_files(istream, datafiles);
    else
        l0->send_chunk_files(istream, datafiles);
}

int main(int argc, char **argv)
{
    //if (argc < 3)
    //usage();

    bool stream = false;
    string filename = "";
    double num_seconds = 0.0;
    double gbps = 0.0;
    vector<string> datafiles;

    CLI::App parser{"ch-frb-simulate-l0 CHIME FRB L0 simulation for testing L1"};
    parser.add_flag("-s,--stream", stream, "Stream files, rather than reading them all in advance.");
    parser.add_option("l0_config", filename, "L0 config filename.yaml")
        ->required()->check(CLI::ExistingFile);
    parser.add_option("num_seconds", num_seconds, "Number of seconds of data to send (if sending noise) or target Gbit/s if sending data")
        ->required();
    parser.add_option("files", datafiles, "Msgpack files to send")
        ->check(CLI::ExistingFile);

    CLI11_PARSE(parser, argc, argv);

    if (datafiles.size())
        gbps = num_seconds;
    else
        if (num_seconds <= 0.0)
            usage();

    shared_ptr<l0_params> p = make_shared<l0_params>(filename, gbps);
    p->write(cout);

    int nthreads = p->nthreads_tot;

    vector<std::thread> threads(nthreads);

    if (datafiles.size() == 0) {
        for (int ithread = 0; ithread < p->nthreads_tot; ithread++)
            threads[ithread] = std::thread(sim_thread_main, p, ithread, num_seconds);
    } else {
        for (int ithread = 0; ithread < p->nthreads_tot; ithread++)
            threads[ithread] = std::thread(chunk_files_thread_main, p, ithread, datafiles, stream);
    }
    
    for (int ithread = 0; ithread < p->nthreads_tot; ithread++)
	threads[ithread].join();

    p->end_streams();
    
    return 0;
}
