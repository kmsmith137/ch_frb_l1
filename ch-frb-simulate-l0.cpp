#include <random>
#include <iostream>
#include <ch_frb_io.hpp>

using namespace std;


static const char *usage_str = 
    "ch-frb-simulate-l0 HOST[:PORT] [target_gbps]\n"
    "   if target_gbps is unspecified, then it will default to 1.0\n"
    "   if target_gbps=0.0 then packets will be sent as fast as possible!\n";


static void usage(const char *extra=nullptr)
{
    cerr << usage_str;

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
    static const int nbeams = 8;
    static const int nfreq_coarse_per_packet = 4;
    static const int fpga_counts_per_sample = 400;   // FIXME double-check this number
    static const int nt_per_packet = 16;
    static const int nupfreq = 16;

    static const vector<int> beam_ids = vrange(nbeams);
    static const vector<int> freq_ids = vrange(ch_frb_io::constants::nfreq_coarse);
    static const double gb_to_simulate = 1.0;
    static const double wt_cutoff = 0.3;    // FIXME what value will make sense here?
    
    //
    // Low-budget command line parsing
    //

    if ((argc < 2) || (argc > 3))
	usage();

    string dstname = argv[1];
    double target_gbps = 1.0;

    if (argc >= 3) {
	try {
	    target_gbps = ch_frb_io::lexical_cast<double> (argv[2]);
	}
	catch (...) {
	    usage();
	}

	if (target_gbps < 0.0)
	    usage("Fatal: invalid value of 'target_gbps'");
    }

    //
    // Make ostream and send data!  (just Gaussian noise for now)
    //

    auto ostream = ch_frb_io::intensity_network_ostream::make(dstname, beam_ids, freq_ids, nupfreq, nt_per_packet, nfreq_coarse_per_packet,
							      nt_per_packet, fpga_counts_per_sample, wt_cutoff, target_gbps);

    int nchunks = int(gb_to_simulate * 1.0e9 / ostream->nbytes_per_chunk) + 1;
    int npackets = nchunks * ostream->npackets_per_chunk;
    int nbytes = nchunks * ostream->nbytes_per_chunk;
    cerr << "ch-frb-simulate-l0: sending " << (nbytes/1.0e9) << " GB data (" << npackets << " packets)\n";

    int chunk_nelts = nbeams * ch_frb_io::constants::nfreq_coarse * nupfreq * nt_per_packet;
    int chunk_stride = nt_per_packet;

    vector<float> intensity(chunk_nelts, 0.0);
    vector<float> weights(chunk_nelts, 1.0);
    
    // for simulating Gaussian noise
    std::random_device rd;
    std::mt19937 rng(rd());
    std::normal_distribution<> dist;

    for (int ichunk = 0; ichunk < nchunks; ichunk++) {
	// randomly simulate chunk
	for (int i = 0; i < chunk_nelts; i++)
	    intensity[i] = dist(rng);

	int64_t fpga_count = int64_t(ichunk) * int64_t(nt_per_packet) * int64_t(fpga_counts_per_sample);
	ostream->send_chunk(&intensity[0], &weights[0], chunk_stride, fpga_count, true);
    }

    ostream->end_stream(true);  // joins network thread

    return 0;
}
