#include <unistd.h>
#include <assert.h>
#include <thread>
#include <random>
#include <iostream>
#include <cstring>
#include <thread>

#include <chlog.hpp>
#include <ch_frb_io.hpp>

#include "l0-sim.hpp"

using namespace std;
using namespace ch_frb_io;

static void usage(const char *extra=nullptr) {
    cerr << "sim-l0-sim [-t target_gbps] [-G gb_to_simulate]\n"
         << "     [-p number of processes]\n"
         << "     [-I bind IP address] (can be repeated)\n"
         << "     [-d destination IP address:port] (repeated for each beam)\n"
         << endl;
    // << "   if -G is unspecified, then gb_to_simulate will default to " << default_gb_to_simulate << "\n"
    // << "   if -t is unspecified, then target_gbps will default to " << ch_frb_io::constants::default_gbps << "\n"
    // << "   if -t 0.0 is specified, then data will be sent as quickly as possible\n"
    // << "   if PORT is unspecified, it will default to " << ch_frb_io::constants::default_udp_port << "\n";

    if (extra)
	cerr << extra << endl;

    exit(2);
}

static vector<int> vrange(int n) {
    vector<int> ret(n);
    for (int i = 0; i < n; i++)
	ret[i] = i;
    return ret;
}

static void run_l0(vector<shared_ptr<L0Simulator> > sims, int irun) {
    chime_log_set_thread_name(stringprintf("L0-sim-%i", irun));
    chlog("Running L0 sims: " << sims.size());
    if (sims.size() == 0)
        return;

    while (1) {
        if (sims[0]->ichunk == sims[0]->nchunks)
            break;
        chlog("Sending chunk " << sims[0]->ichunk);
        for (auto it=sims.begin(); it!=sims.end(); it++)
            (*it)->send_one_chunk();
    }
    chlog("Finishing...");
    for (auto it=sims.begin(); it!=sims.end(); it++)
        (*it)->finish();
    chlog("Done running L0 sims");
}

int main(int argc, char **argv) {

    double gb_to_sim = 0.0;
    double drop_rate = 0.0;

    int nproc = 1;

    vector<string> source_ips;
    vector<string> dests;

    ch_frb_io::intensity_network_ostream::initializer ini;
    //ini_params.beam_ids = vrange(8);
    ini.coarse_freq_ids = vrange(ch_frb_io::constants::nfreq_coarse_tot);
    ini.nupfreq = 16;
    ini.nfreq_coarse_per_packet = 4;
    ini.nt_per_packet = 16;
    ini.nt_per_chunk = 16;
    ini.fpga_counts_per_sample = 400;   // FIXME double-check this number

    int c;
    while ((c = getopt(argc, argv, "t:G:p:I:d:h")) != -1) {
        switch (c) {
        case 't':
            ini.target_gbps = atof(optarg);
            break;

        case 'G':
            gb_to_sim = atof(optarg);
            break;

        case 'p':
            nproc = atoi(optarg);
            break;

        case 'I':
            source_ips.push_back(string(optarg));
            break;

        case 'd':
            dests.push_back(string(optarg));
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

    if (dests.size() == 0) {
        usage();
        return 0;
    }

    vector<shared_ptr<L0Simulator> > sims;

    vector<thread*> threads;
    int i = 0;
    int irun = 0;
    for (auto it = dests.begin(); it != dests.end(); it++, i++) {
        int beam_id = i;
        ini.beam_ids.clear();
        ini.beam_ids.push_back(beam_id);

        ini.dstname = *it;

        if (source_ips.size())
            ini.bind_ip = source_ips[i % source_ips.size()];

        shared_ptr<L0Simulator> sim = make_shared<L0Simulator>(ini, gb_to_sim, drop_rate);
        sims.push_back(sim);

        if ((i+1) % (dests.size() / nproc) == 0) {
            // Start a thread to process these ones.
            std::thread* t = new thread(std::bind(run_l0, sims, irun));
            irun++;
            threads.push_back(t);
            sims.clear();
        }
    }

    assert(sims.size() == 0);

    for (auto it=threads.begin(); it != threads.end(); it++) {
        (*it)->join();
    }
}
