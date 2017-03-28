#include <cassert>
#include <thread>

#include <ch_frb_io.hpp>
#include <ch_frb_io_internals.hpp>

#include "ch_frb_l1.hpp"

using namespace std;
using namespace ch_frb_l1;

using ch_frb_io::lexical_cast;



// -------------------------------------------------------------------------------------------------
//
// l0_params


struct l0_params {
    static constexpr int nfreq_coarse_tot = ch_frb_io::constants::nfreq_coarse_tot;

    l0_params(const string &filename);

    shared_ptr<ch_frb_io::intensity_network_ostream> make_ostream(int ithread) const;

    void write(ostream &os) const;

    int nbeams_tot = 0;
    int nthreads_tot = 0;
    int nupfreq = 0;

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
    
    // Optional params.  These will be assigned reasonable defaults if not specified in the config file.
    // Note: the total number of coarse frequencies is hardcoded (1024), but the number of coarse frequencies per
    // packet is a configurable parameter.
    int nfreq_coarse_per_packet = 0;
    int nt_per_packet = 0;

    // Derived parameters
    int nbeams_per_stream = 0;
    int nthreads_per_stream = 0;
    int nfreq_coarse_per_thread = 0;
    int packet_size = 0;
    double gbps_per_thread = 0.0;
    double gbps_tot = 0.0;
};


l0_params::l0_params(const string &filename)
{
    yaml_paramfile p(filename);

    this->nbeams_tot = p.read_scalar<int> ("nbeams");
    this->nthreads_tot = p.read_scalar<int> ("nthreads");
    this->nupfreq = p.read_scalar<int> ("nupfreq");

    if (p.has_param("fpga_counts_per_sample"))
	this->fpga_counts_per_sample = p.read_scalar<int> ("fpga_counts_per_sample");
    if (p.has_param("max_packet_size"))
	this->max_packet_size = p.read_scalar<int> ("max_packet_size");

    this->ipaddr = p.read_vector<string> ("ipaddr");
    this->port = p.read_vector<int> ("port");

    if ((ipaddr.size() == 1) && (port.size() > 1))
	this->ipaddr = vector<string> (port.size(), ipaddr[0]);
    else if ((ipaddr.size() > 1) && (port.size() == 1))
	this->port = vector<int> (ipaddr.size(), port[0]);

    if (ipaddr.size() != port.size())
	throw runtime_error(filename + " expected 'ip_addr' and 'port' to be lists of equal length");

    this->nstreams = ipaddr.size();

    assert(nstreams > 0);
    assert(nthreads_tot > 0);
    assert(nthreads_tot <= 32);
    assert(nbeams_tot > 0);
    assert(nupfreq > 0);
    assert(fpga_counts_per_sample > 0);
    assert(max_packet_size > 0);
    assert(ipaddr.size() == (unsigned int)nstreams);
    assert(port.size() == (unsigned int)nstreams);

    for (int i = 0; i < nstreams; i++)
	assert((port[i] > 0) && (port[i] < 65536));

    if (nbeams_tot % nstreams != 0) {
	throw runtime_error(filename + ": nbeams (=" + to_string(nbeams_tot) + ") must be a multiple of nstreams (=" 
			    + to_string(nstreams) + ", inferred by counting (ipaddr,port) pairs)");
    }

    if (nthreads_tot % nstreams != 0) {
	throw runtime_error(filename + ": nthreads (=" + to_string(nthreads_tot) + ") must be a multiple of nstreams (=" 
			    + to_string(nstreams) + ", inferred by counting (ipaddr,port) pairs)");
    }

    if (!is_power_of_two(xdiv(nthreads_tot,nstreams))) {
	throw runtime_error(filename + ": nthreads (=" + to_string(nthreads_tot) + ") must be a power of two times nstreams (=" 
			    + to_string(nstreams) + ", inferred by counting (ipaddr,port) pairs)");
    }

    // Derived parameters, part 1
    this->nbeams_per_stream = xdiv(nbeams_tot, nstreams);
    this->nthreads_per_stream = xdiv(nthreads_tot, nstreams);
    this->nfreq_coarse_per_thread = xdiv(nfreq_coarse_tot, nthreads_per_stream);

    // Logic for nfreq_coarse_per_packet, nt_per_packet can be a little complicated!

    if (p.has_param("nfreq_coarse_per_packet") && p.has_param("nt_per_packet")) {
	// Case 1: both parameters are specified in file.
	this->nfreq_coarse_per_packet = p.read_scalar<int> ("nfreq_coarse_per_packet");
	this->nt_per_packet = p.read_scalar<int> ("nt_per_packet");

	if ((nfreq_coarse_per_packet <= 0) || (nfreq_coarse_per_thread % nfreq_coarse_per_packet)) {
	    throw runtime_error(filename + ": nfreq_coarse_per_packet(=" + to_string(nfreq_coarse_per_packet)
				+ " must be > 0 and evenly divide nfreq_coarse_per_thread(=" 
				+ to_string(nfreq_coarse_per_thread) + ")");
	}

	if ((nt_per_packet <= 0) || !is_power_of_two(nt_per_packet))
	    throw runtime_error(filename + ": nt_per_packet(=" + to_string(nt_per_packet) + " must be a power of two");

	int p = ch_frb_io::intensity_packet::packet_size(nbeams_per_stream, nfreq_coarse_per_packet, nupfreq, nt_per_packet);
	
	if (p > max_packet_size) {
	    throw runtime_error(filename + ": computed packet size (=" + to_string(p) 
				+ ") exceeds max_packet_size (=" + to_string(max_packet_size));
	}
    }
    else if (!p.has_param("nfreq_coarse_per_packet") && !p.has_param("nt_per_packet")) {
	// Case 2: neither parameter is specified in file

	// Heuristically determined 
	double f = 0.0027 * max_packet_size / double(nbeams_per_stream);
	this->nfreq_coarse_per_packet = min(round_up_to_power_of_two(f), nfreq_coarse_per_thread);

	int h = ch_frb_io::intensity_packet::header_size(nbeams_per_stream, nfreq_coarse_per_packet);
	double t = (max_packet_size - h) / double(nbeams_per_stream * nfreq_coarse_per_packet * nupfreq);

	if (t < 1.01) {
	    throw runtime_error(filename + ": internal error: couldn't determine nt_per_packet.  Maybe max_packet_size (="
				+ to_string(max_packet_size) + ") is too small?");
	}

	this->nt_per_packet = round_down_to_power_of_two(t);
    }
    else
	throw runtime_error(filename + ": currently, either both parameters 'nfreq_coarse_per_packet', 'nt_per_packet' must be specified, or neither parameter");

    // Derived parameters, part 2
    this->packet_size = ch_frb_io::intensity_packet::packet_size(nbeams_per_stream, nfreq_coarse_per_packet, nupfreq, nt_per_packet);

    double num = (8.0e-9 * packet_size) * (nfreq_coarse_per_thread / double(nfreq_coarse_per_packet));
    double den = (ch_frb_io::constants::dt_fpga * fpga_counts_per_sample * nt_per_packet);
    this->gbps_per_thread = num/den;
    this->gbps_tot = nthreads_tot * gbps_per_thread;

    p.check_for_unused_params();

    assert(nfreq_coarse_per_packet > 0);
    assert(nfreq_coarse_per_thread % nfreq_coarse_per_packet == 0);
    assert(nt_per_packet > 0);
    assert(is_power_of_two(nt_per_packet));
    assert(packet_size <= max_packet_size);
}


shared_ptr<ch_frb_io::intensity_network_ostream> l0_params::make_ostream(int ithread) const
{
    assert(ithread >= 0 && ithread < nthreads_tot);

    int istream = ithread / nthreads_per_stream;
    int jthread = ithread % nthreads_per_stream;

    ch_frb_io::intensity_network_ostream::initializer ini_params;
    ini_params.dstname = ipaddr[istream] + ":" + to_string(port[istream]);
    ini_params.beam_ids = vrange(istream * nbeams_per_stream, (istream+1) * nbeams_per_stream);
    ini_params.coarse_freq_ids = vrange(jthread * nfreq_coarse_per_thread, (jthread+1) * nfreq_coarse_per_thread);
    ini_params.nupfreq = nupfreq;
    ini_params.nt_per_chunk = nt_per_packet;   // best?
    ini_params.nfreq_coarse_per_packet = nfreq_coarse_per_packet;
    ini_params.nt_per_packet = nt_per_packet;
    ini_params.fpga_counts_per_sample = fpga_counts_per_sample;
    ini_params.send_end_of_stream_packets = (jthread == 0);   // only one distinguished thread will send end-of-stream packets
    ini_params.print_status_at_end = false;

    return ch_frb_io::intensity_network_ostream::make(ini_params);
}


void l0_params::write(ostream &os) const
{
    os << "nbeams_tot = " << nbeams_tot << "\n"
       << "nthreads_tot = " << nthreads_tot << "\n"
       << "nupfreq = " << nupfreq << "\n"
       << "nstreams = " << nstreams << "\n"
       << "streams = [";

    for (int i = 0; i < nstreams; i++)
	os << " " << ipaddr[i] << ":" << port[i];

    os << "]\n"
       << "fpga_counts_per_sample = " << fpga_counts_per_sample << "\n"
       << "max_packet_size = " << max_packet_size << "\n"
       << "nfreq_coarse_per_packet = " << nfreq_coarse_per_packet << "\n"
       << "nt_per_packet = " << nt_per_packet << "\n"
       << "nbeams_per_stream = " << nbeams_per_stream << "\n"
       << "nthreads_per_stream = " << nthreads_per_stream << "\n"
       << "nfreq_coarse_per_thread = " << nfreq_coarse_per_thread << "\n"
       << "packet_size = " << packet_size << "\n"
       << "gbps_per_thread = " << gbps_per_thread << "\n"
       << "gbps_tot = " << gbps_tot << "\n";
}


// -------------------------------------------------------------------------------------------------


void sim_thread_main(const shared_ptr<ch_frb_io::intensity_network_ostream> &ostream, double num_seconds)
{
    assert(ostream->target_gbps > 0.0);

    double target_nbytes = 1.25e8 * ostream->target_gbps * num_seconds;
    int nchunks = int(target_nbytes / ostream->nbytes_per_chunk) + 1;

    vector<float> intensity(ostream->elts_per_chunk, 0.0);
    vector<float> weights(ostream->elts_per_chunk, 1.0);
    int stride = ostream->nt_per_packet;

    // After some testing (in branch uniform-rng, on frb-compute-0),
    // found that std::ranlux48_base is the fastest of the std::
    // builtin random number generators.
    std::random_device rd;
    unsigned int seed = rd();
    std::ranlux48_base rando(seed);

    for (int ichunk = 0; ichunk < nchunks; ichunk++) {
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

    // We don't call ostream->end_stream() here.  This is because end_stream() has the side effect
    // of sending end-of-stream packets in one distinguished thread (see above).  We want to make
    // sure that all threads have finished transmitting before the end-of-stream packets are sent.
    //
    // Therefore, we postpone the call to ostream->end_stream() until all sim_threads have finished
    // and joined (see main() below).
}


static void usage()
{
    cerr << "usage: ch-frb-simulate-l0 <l0_params.yaml> <num_seconds>\n";
    exit(2);
}


int main(int argc, char **argv)
{
    if (argc != 3)
	usage();

    string filename = argv[1];
    double num_seconds = lexical_cast<double> (argv[2]);

    if (num_seconds <= 0.0)
	usage();

    l0_params p(filename);
    p.write(cout);

    int nthreads = p.nthreads_tot;

    vector<shared_ptr<ch_frb_io::intensity_network_ostream>> streams(nthreads);
    vector<std::thread> threads(nthreads);

    for (int ithread = 0; ithread < p.nthreads_tot; ithread++) {
	streams[ithread] = p.make_ostream(ithread);
	streams[ithread]->print_status();
    }

    for (int ithread = 0; ithread < p.nthreads_tot; ithread++)
	threads[ithread] = std::thread(sim_thread_main, streams[ithread], num_seconds);

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
