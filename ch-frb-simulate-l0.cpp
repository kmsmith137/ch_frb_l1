// This is a toy program which simulates a packet stream at the full CHIME data rate,
// for timing purposes.  The receiver will usually be the counterpart toy program 'ch-frb-l1'.
//
// It can be pointed at any IP address, but we usually just run over the loopback IP 127.0.0.1.
//
// A major caveat is that the intensity values it sends are just arbitrary numbers, not
// something more interesting like Gaussian random noise, or even more interesting such
// as noise + simulated FRB's.  This is because the current simulation code is too slow
// to simulate anything interesting (even Gaussian noise) at the full CHIME data rate,
// and fixing this is a to-do item (see discussion in ch_frb_io/README).

#include <random>
#include <iostream>
#include <cstring>
#include <ch_frb_io.hpp>

using namespace std;
using ch_frb_io::lexical_cast;

static const double default_gb_to_simulate = 10.0;

static void usage(const char *extra=nullptr)
{
    cerr << "ch-frb-simulate-l0 [-t target_gbps] [-G gb_to_simulate] HOST[:PORT]\n"
	 << "   if -G is unspecified, then gb_to_simulate will default to " << default_gb_to_simulate << "\n"
	 << "   if -t is unspecified, then target_gbps will default to " << ch_frb_io::constants::default_gbps << "\n"
	 << "   if -t 0.0 is specified, then data will be sent as quickly as possible\n"
	 << "   if PORT is unspecified, it will default to " << ch_frb_io::constants::default_udp_port << "\n";

    if (extra)
	cerr << extra << endl;

    exit(2);
}

// misc helper
static vector<int> vrange(int n)
{
    vector<int> ret(n);
    for (int i = 0; i < n; i++)
	ret[i] = i;
    return ret;
}


int main(int argc, char **argv)
{
    double gb_to_simulate = default_gb_to_simulate;

    ch_frb_io::intensity_network_ostream::initializer ini_params;
    ini_params.beam_ids = vrange(8);
    ini_params.coarse_freq_ids = vrange(ch_frb_io::constants::nfreq_coarse_tot);
    ini_params.nupfreq = 16;
    ini_params.nfreq_coarse_per_packet = 4;
    ini_params.nt_per_packet = 16;
    ini_params.nt_per_chunk = 16;
    ini_params.fpga_counts_per_sample = 400;   // FIXME double-check this number

    // Low-budget command line parsing

    int iarg = 1;

    while (iarg < argc) {
	if (!strcmp(argv[iarg], "-t")) {
	    if (iarg >= argc-1)
		usage();
	    if (!lexical_cast(argv[iarg+1], ini_params.target_gbps))
		usage();
	    iarg += 2;
	}
	else if (!strcmp(argv[iarg], "-G")) {
	    if (iarg >= argc-1)
		usage();
	    if (!lexical_cast(argv[iarg+1], gb_to_simulate))
		usage();
	    if (gb_to_simulate <= 0.0)
		usage();
	    iarg += 2;
	}	    
	else {
	    if (ini_params.dstname.size() > 0)
		usage();
	    ini_params.dstname = argv[iarg];
	    iarg++;
	}
    }

    if (ini_params.dstname.size() == 0)
	usage();

    // Make output stream object and print a little summary info

    auto ostream = ch_frb_io::intensity_network_ostream::make(ini_params);

    int nchunks = int(gb_to_simulate * 1.0e9 / ostream->nbytes_per_chunk) + 1;
    int npackets = nchunks * ostream->npackets_per_chunk;
    int nbytes = nchunks * ostream->nbytes_per_chunk;
    cerr << "ch-frb-simulate-l0: sending " << (nbytes/1.0e9) << " GB data (" << nchunks << " chunks, " << npackets << " packets)\n";

    vector<float> intensity(ostream->elts_per_chunk, 0.0);
    vector<float> weights(ostream->elts_per_chunk, 1.0);
    int stride = ostream->nt_per_packet;

    // After some testing (in branch uniform-rng, on frb-compute-0),
    // found that std::ranlux48_base is the fastest of the std::
    // builtin random number generators.
    std::random_device rd;
    unsigned int seed = rd();
    std::ranlux48_base rando(seed);
    //cout << "range " << rando.min() << " to " << rando.max() << endl;
    // Range is 2**48.

    // Thought: if we pre-scaled to uint8_t, maybe we can get 6 random
    // numbers per call by pulling out individual bytes...  Would have
    // to modify the packet.encode() method to do this.

#if 0
    // I'd like to simulate Gaussian noise, but the Gaussian random number generation 
    // actually turns out to be a bottleneck!
    std::mt19937 rng(rd());
    std::normal_distribution<> dist;
#endif

    // Send data.  The output stream object will automatically throttle packets to its target bandwidth.

    for (int ichunk = 0; ichunk < nchunks; ichunk++) {
        // To avoid the cost of simulating Gaussian noise, we use the following semi-arbitrary procedure.
        //for (unsigned int i = 0; i < intensity.size(); i++)
        //intensity[i] = ichunk + i;

        // Hackily scale the integer random number generator to
        // produce uniform numbers in [mean - 2 sigma, mean + 2 sigma].
        float mean = 100;
        float stddev = 40;
        float r0 = mean - 2.*stddev;
        float scale = 4.*stddev / (rando.max() - rando.min());
        for (unsigned int i = 0; i < intensity.size(); i++)
            intensity[i] = r0 + scale * (float)rando();

	int64_t fpga_count = int64_t(ichunk) * int64_t(ostream->fpga_counts_per_chunk);
	ostream->send_chunk(&intensity[0], &weights[0], stride, fpga_count);
    }


    // All done!

    ostream->end_stream(true);  // "true" joins network thread

    return 0;
}
