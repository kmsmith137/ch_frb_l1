// Major features missing:
//
//   - RFI removal is a placeholder
//   - Coarse-grained trigger output stream is a placeholder
//   - RPC threads currently are not spawned
//   - Logging threads currently are not spawned.

#include <thread>

#include <ch_frb_io.hpp>
#include <rf_pipelines.hpp>
#include <bonsai.hpp>

#include "ch_frb_l1.hpp"

using namespace std;
using namespace ch_frb_l1;


// -------------------------------------------------------------------------------------------------
//
// More config parameters to come:
//  - Parameters needed for RPC's (e.g. RPC port number)
//  - Parameters defining location of sifting/grouping code (e.g. local socket)


struct l1_params {
    l1_params(const string &filename);

    // Input stream object reads UDP packets from correlator.
    shared_ptr<ch_frb_io::intensity_network_stream> make_input_stream(int istream);

    // Output stream object writes coarse-grained triggers to grouping/sifting code.
    shared_ptr<bonsai::trigger_output_stream> make_output_stream(int ibeam);

    // nstreams is automatically determined by the number of (ipaddr, port) pairs.
    // There will be one (network_thread, assembler_thread) pair for each stream.
    int nbeams = 0;
    int nstreams = 0;

    // Both vectors have length nstream.
    vector<string> ipaddr;
    vector<int> port;
};


l1_params::l1_params(const string &filename)
{
    yaml_paramfile p(filename);
     
    this->nbeams = p.read_scalar<int> ("nbeams");
    this->ipaddr = p.read_vector<string> ("ipaddr");
    this->port = p.read_vector<int> ("port");
    
    if ((ipaddr.size() == 1) && (port.size() > 1))
	this->ipaddr = vector<string> (port.size(), ipaddr[0]);
    else if ((ipaddr.size() > 1) && (port.size() == 1))
	this->port = vector<int> (ipaddr.size(), port[0]);
    
    if (ipaddr.size() != port.size())
	throw runtime_error(filename + " expected 'ip_addr' and 'port' to be lists of equal length");
    
    this->nstreams = ipaddr.size();

    assert(nbeams > 0);
    assert(nstreams > 0);
    assert(ipaddr.size() == (unsigned int)nstreams);
    assert(port.size() == (unsigned int)nstreams);

    if (nbeams % nstreams) {
	throw runtime_error(filename + " nbeams (=" + to_string(nbeams) + ") must be a multiple of nstreams (="
			    + to_string(nstreams) + ", inferred from number of (ipaddr,port) pairs");
    }

    p.check_for_unused_params();
}


// l1_params::make_input_stream(): returns a stream object which will read packets from the correlator.
//
// Currently hardcoded to assume the following NUMA setup:
//   - dual cpu
//   - 10 cores/cpu
//   - all NIC's on the same PCI-E bus as the first CPU.


// Assuming hyperthreading is enabled, these core_lists can be used to pin threasd
// either to the first or second CPU.
static vector<int> cpu1_cores() { return vconcat(vrange(0,10), vrange(20,30)); }
static vector<int> cpu2_cores() { return vconcat(vrange(10,20), vrange(30,40)); }


shared_ptr<ch_frb_io::intensity_network_stream> l1_params::make_input_stream(int istream)
{
    assert(istream >= 0 && istream < nstreams);

    if (std::thread::hardware_concurrency() != 40)
	throw runtime_error("ch-frb-l1: this program is currently hardcoded to run on the 20-core chimefrb test nodes, and won't run on smaller machines");

    if (nstreams % 2 == 1)
	throw runtime_error("ch-frb-l1: nstreams must be even, in order to divide dedispersion threads evenly between the two CPUs");

    ch_frb_io::intensity_network_stream::initializer ini_params;

    ini_params.ipaddr = ipaddr[istream];
    ini_params.udp_port = port[istream];
    ini_params.beam_ids = vrange(4*istream, 4*(istream+1));

    // Note: network thread is always pinned to the first CPU,
    // but the assembler thread is pinned to whichever CPU will
    // run the dedispersion threads.

    ini_params.network_thread_cores = cpu1_cores();
    ini_params.assembler_thread_cores = (istream < (nstreams/2)) ? cpu1_cores() : cpu2_cores();

    return ch_frb_io::intensity_network_stream::make(ini_params);
}


// l1_params::make_output_stream(): returns the stream object which will send coarse-grained
// triggers to the sifting/grouping code.
//
// Currently a placeholder, since the "output stream" object doesn't send the triggers
// anywhere, it just keeps a count of chunks processed.

shared_ptr<bonsai::trigger_output_stream> l1_params::make_output_stream(int ibeam)
{
    assert(ibeam >= 0 && ibeam < nbeams);
    return make_shared<bonsai::trigger_output_stream> ();
}


// -------------------------------------------------------------------------------------------------


// make_rfi_chain(): currently a placeholder which returns an arbitrarily constructed transform chain.
//
// The long-term plan here is:
//   - keep developing RFI removal, until all transforms are C++
//   - write code to serialize a C++ transform chain to yaml
//   - add a command-line argument <transform_chain.yaml> to ch-frb-l1 


static vector<shared_ptr<rf_pipelines::wi_transform>> make_rfi_chain()
{
    int nt_chunk = 1024;
    int polydeg = 2;

    auto t1 = rf_pipelines::make_polynomial_detrender(nt_chunk, rf_pipelines::AXIS_FREQ, polydeg);
    auto t2 = rf_pipelines::make_polynomial_detrender(nt_chunk, rf_pipelines::AXIS_TIME, polydeg);
    
    return { t1, t2 };
}


// A little helper routine to make the bonsai_dedisperser 
// (Returns the rf_pipelines::wi_transform wrapper object, not the bonsai::dedisperser)
static shared_ptr<rf_pipelines::wi_transform> make_dedisperser(const bonsai::config_params &cp, const shared_ptr<bonsai::trigger_output_stream> &tp)
{
    bonsai::dedisperser::initializer ini_params;
    ini_params.verbosity = 0;
    
    auto d = make_shared<bonsai::dedisperser> (cp, ini_params);
    d->add_processor(tp);

    return rf_pipelines::make_bonsai_dedisperser(d);
}


// -------------------------------------------------------------------------------------------------


static void dedispersion_thread_main(const l1_params &l1_config, const bonsai::config_params &cp,
				     const shared_ptr<ch_frb_io::intensity_network_stream> &sp, 
				     const shared_ptr<bonsai::trigger_output_stream> &tp,
				     int ibeam)
{
    // FIXME write a std::thread subclass which makes this try..catch logic automatic.

    try {
	if (std::thread::hardware_concurrency() != 40)
	    throw runtime_error("ch-frb-l1: this program is currently hardcoded to run on the 20-core chimefrb test nodes, and won't run on smaller machines");
	
	if (l1_config.nbeams != 16)
	    throw runtime_error("ch-frb-l1: current core-pinning logic in dedispersion_thread_main() assumes 16 threads");
	
	int c = (l1_config.nbeams / 8) * 10 + (l1_config.nbeams % 8);
	ch_frb_io::pin_thread_to_cores({c,c+20});
	
	auto stream = rf_pipelines::make_chime_network_stream(sp, ibeam);
	auto dedisperser = make_dedisperser(cp, tp);
	auto transform_chain = make_rfi_chain();
	
	transform_chain.push_back(dedisperser);
	stream->run(transform_chain);
    } catch (exception &e) {
	cerr << e.what() << "\n";
	throw;
    }
}


static void usage()
{
    cerr << "usage: ch-frb-l1 <l1_config.yaml> <bonsai_config.txt>\n";
    exit(2);
}


int main(int argc, char **argv)
{
    if (argc != 3)
	usage();

    l1_params l1_config(argv[1]);
    bonsai::config_params bonsai_config(argv[2]);

    int nstreams = l1_config.nstreams;
    int nbeams = l1_config.nbeams;

    vector<shared_ptr<ch_frb_io::intensity_network_stream>> input_streams(nstreams);
    vector<shared_ptr<bonsai::trigger_output_stream>> output_streams(nbeams);
    vector<std::thread> threads(nbeams);


    for (int istream = 0; istream < nstreams; istream++)
	input_streams[istream] = l1_config.make_input_stream(istream);

    for (int ibeam = 0; ibeam < nbeams; ibeam++)
	output_streams[ibeam] = l1_config.make_output_stream(ibeam);

    for (int ibeam = 0; ibeam < nbeams; ibeam++) {
	cerr << "spawning thread " << ibeam << endl;
	int nbeams_per_stream = xdiv(nbeams, nstreams);
	int istream = ibeam / nbeams_per_stream;
	threads[ibeam] = std::thread(dedispersion_thread_main, l1_config, bonsai_config, input_streams[istream], output_streams[ibeam], ibeam);

	// FIXME temporary!
	sleep(1);
    }

    for (int ibeam = 0; ibeam < nbeams; ibeam++)
	threads[ibeam].join();

    for (int istream = 0; istream < nstreams; istream++) {
	cout << "stream " << istream << ": ipaddr=" << l1_config.ipaddr[istream] << ", udp_port=" << l1_config.port[istream] << endl;

	// vector<map<string,int>>
	auto statistics = input_streams[istream]->get_statistics();

	for (unsigned int irec = 0; irec < statistics.size(); irec++) {
	    const auto &s = statistics[irec];

	    vector<string> keys;
	    for (const auto &kv: s)
		keys.push_back(kv.first);
	    
	    sort(keys.begin(), keys.end());
	
	    for (const auto &k: keys) {
		auto kv = s.find(k);
		cout << "    " << k << " " << kv->second << endl;
	    }
	}
    }

    for (int ibeam = 0; ibeam < nbeams; ibeam++)
	cout << "dedisperser " << ibeam << ": nchunks_processed=" << output_streams[ibeam]->nchunks_processed << "\n";

    return 0;
}
