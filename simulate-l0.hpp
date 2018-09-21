#include <cassert>
#include <thread>

#include <ch_frb_io.hpp>
#include <ch_frb_io_internals.hpp>

#include "ch_frb_l1.hpp"

using namespace std;

// -------------------------------------------------------------------------------------------------
//
// l0_params


struct l0_params {
    static constexpr int nfreq_coarse = ch_frb_io::constants::nfreq_coarse_tot;

    l0_params(const string &filename);

    shared_ptr<ch_frb_io::intensity_network_ostream> make_ostream(int ithread, double gbps) const;

    void write(ostream &os) const;

    int nbeams_tot = 0;
    int nthreads_tot = 0;
    int nfreq_fine = 0;
    int nt_per_packet = 0;

    // The 'ipaddr' and 'port' vectors have the same length 'nstreams'
    // nstreams evenly divides nthreads.
    // nstreams evenly divides nbeams.
    vector<string> ipaddr;
    vector<int> port;
    int nstreams = 0;

    // Optional params with hardcoded defaults
    // Note: fpga_counts_per_sample=384 corresponds to ~1ms sampling
    // Note: max_packet_size=8900 is appropriate for 9000-byte jumbo ethernet frames, minus 100 bytes for IP and UDP headers.
    int fpga_counts_per_sample = 384;
    int max_packet_size = 8900;

    // Optional, will be assigned a reasonable default if not specified in the config file.
    int nfreq_coarse_per_packet = 0;

    // Derived parameters
    int nupfreq = 0;
    int nbeams_per_stream = 0;
    int nthreads_per_stream = 0;
    int nfreq_coarse_per_thread = 0;
    int packet_size = 0;

};



void sim_thread_main(const shared_ptr<ch_frb_io::intensity_network_ostream> &ostream, double num_seconds);

void data_thread_main(const shared_ptr<ch_frb_io::intensity_network_ostream> &ostream,
                      const vector<string> &filenames);


