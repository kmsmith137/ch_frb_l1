// Major features missing:
//
//   - RFI removal is a placeholder
//   - Distributed logging is not integrated
//   - If anything goes wrong, the L1 server will crash!
//
// The L1 server can run in two modes: either a "full-scale" mode with 16 beams and 20 cores,
// or a "subscale" mode with (nbeams <= 4) and no core-pinning.
//
// The "full-scale" mode is hardcoded to assume the NUMA setup of the CHIMEFRB L1 nodes:
//   - Dual CPU
//   - 10 cores/cpu
//   - Hyperthreading enabled
//   - all NIC's on the same PCI-E bus as the first CPU.
//
// Note that the Linux scheduler defines 40 "cores":
//   cores 0-9:    primary hyperthread on CPU1 
//   cores 10-19:  primary hyperthread on CPU2
//   cores 20-29:  secondary hyperthread on CPU1
//   cores 30-39:  secondary hyperthread on CPU2

#include <thread>
#include <fstream>

#include <ch_frb_io.hpp>
#include <rf_pipelines.hpp>
#include <bonsai.hpp>
#include <l1-rpc.hpp>

#include "ch_frb_l1.hpp"
#include "chlog.hpp"

using namespace std;
using namespace ch_frb_l1;


static void usage()
{
    cerr << "Usage: ch-frb-l1 [-vp] <l1_config.yaml> <rfi_config.txt> <bonsai_config.txt> <l1b_config_file>\n"
	 << "  The -v flag increases verbosity of the toplevel ch-frb-l1 logic\n"
	 << "  The -p flag enables a very verbose debug trace of the pipe I/O between L1a and L1b\n"
	 << "  The -f flag forces the L1 server to run, even if the config files look fishy\n";

    exit(2);
}


// -------------------------------------------------------------------------------------------------


struct l1_params {
    l1_params(int argc, char **argv);

    string l1_config_filename;
    string rfi_config_filename;
    string bonsai_config_filename;
    string l1b_config_filename;

    Json::Value rfi_transform_chain_json;
    bonsai::config_params bonsai_config;

    // l1_verbosity=1: pretty quiet
    // l1_verbosity=2: pretty noisy
    // I may add l1_verbosity=0 and l1_verbosity=3 later!
    int l1_verbosity = 1;
    bool l1b_pipe_io_debug = false;
    bool fflag = false;

    // nstreams is automatically determined by the number of (ipaddr, port) pairs.
    // There will be one (network_thread, assembler_thread, rpc_server) triple for each stream.
    int nbeams = 0;
    int nstreams = 0;

    // The L1 server can run in two modes: either a "full-scale" mode with 16 beams and 20 cores,
    // or a "subscale" mode with (nbeams <= 4) and no core-pinning.
    bool is_subscale = true;
    
    // If slow_kernels=false (the default), the L1 server will use fast assembly language kernels
    // for its packet processing.  If slow_kernels=true, then it will use reference kernels which
    // are much slower.
    //
    // Note 1: the slow kernels are too slow for non-subscale use!  If slow kernels are used on
    // the "full-scale" L1 server with (nbeams, nupfreq) = (16, 16), it may crash.
    //
    // Note 2: the fast kernels can only be used if certain conditions are met.  As of this writing,
    // the conditions are: (a) nupfreq must be even, and (b) nt_per_packet must be 16.  In particular,
    // for a subscale run with nupfreq=1, the fast kernels can't be used.
    //
    // Conditions (a) and (b) could be generalized by writing a little more code, if this would be useful
    // then let me know!
    bool slow_kernels = false;

    // Both vectors have length nstreams.
    vector<string> ipaddr;
    vector<int> port;

    // One L1-RPC per stream
    vector<string> rpc_address;

    // A vector of length nbeams, containing the beam_ids that will be processed on this L1 server.
    // It is currently assumed that these are known in advance and never change!
    // If unspecified, 'beam_ids' defaults to { 0, ..., nbeams-1 }.
    vector<int> beam_ids;

    // L1b linkage.  Note: assumed L1b command line is:
    //   <l1b_executable_filename> <l1b_config_filename> <beam_id>

    std::string l1b_executable_filename;
    bool l1b_search_path = false;     // will $PATH be searched for executable?
    int l1b_buffer_nsamples = 0;      // determines buffer size between L1a and L1b (0 = system default)
    double l1b_pipe_timeout = 0.0;    // timeout in seconds between L1a and L1
    bool l1b_pipe_blocking = false;   // setting this to true is equivalent to a very large l1b_pipe_timeout

    // Occasionally useful for debugging: If track_global_trigger_max is true, then when 
    // the L1 server exits, it will print the (DM, arrival time) of the most significant FRB.

    bool track_global_trigger_max = false;

    // Helper function for constructor
    void die_unless_fflag_set(const string &msg) const;
};


l1_params::l1_params(int argc, char **argv)
{
    vector<string> args;

    // Low-budget command line parsing

    for (int i = 1; i < argc; i++) {
	if (argv[i][0] != '-') {
	    args.push_back(argv[i]);
	    continue;
	}

	for (int j = 1; argv[i][j] != 0; j++) {
	    if (argv[i][j] == 'v')
		this->l1_verbosity = 2;
	    else if (argv[i][j] == 'p')
		this->l1b_pipe_io_debug = true;
	    else if (argv[i][j] == 'f')
		this->fflag = true;
	    else
		usage();
	}
    }

    if (args.size() != 4)
	usage();

    this->l1_config_filename = args[0];
    this->rfi_config_filename = args[1];
    this->bonsai_config_filename = args[2];
    this->l1b_config_filename = args[3];

    // Read rfi_config file.
    std::ifstream rfi_config_file(rfi_config_filename);
    if (rfi_config_file.fail())
        throw runtime_error("ch-frb-l1: couldn't open file " + rfi_config_filename);

    Json::Reader rfi_config_reader;
    if (!rfi_config_reader.parse(rfi_config_file, this->rfi_transform_chain_json))
	throw runtime_error("ch-frb-l1: couldn't parse json file " + rfi_config_filename);

    // Throwaway call, to get an early check that rfi_config_file is valid.
    auto rfi_chain = rf_pipelines::deserialize_transform_chain_from_json(this->rfi_transform_chain_json);

    if (l1_verbosity >= 2) {
	cout << rfi_config_filename << ": " << rfi_chain.size() << " transforms\n";
	for (unsigned int i = 0; i < rfi_chain.size(); i++)
	    cout << rfi_config_filename << ": transform " << i << "/" << rfi_chain.size() << ": " << rfi_chain[i]->name << "\n";
    }

    // Read bonsai_config file.
    this->bonsai_config = bonsai::config_params(bonsai_config_filename);

    if (l1_verbosity >= 2) {
	bool write_derived_params = true;
	string prefix = bonsai_config_filename + ": ";
	bonsai_config.write(cout, write_derived_params, prefix);
    }

    // Remaining code in this function reads l1_config file.

    int yaml_verbosity = (this->l1_verbosity >= 2) ? 1 : 0;
    yaml_paramfile p(l1_config_filename, yaml_verbosity);

    // These parameters can be read right away.
    this->nbeams = p.read_scalar<int> ("nbeams");
    this->ipaddr = p.read_vector<string> ("ipaddr");
    this->port = p.read_vector<int> ("port");
    this->rpc_address = p.read_vector<string> ("rpc_address");
    this->slow_kernels = p.read_scalar<bool> ("slow_kernels", false);
    this->l1b_executable_filename = p.read_scalar<string> ("l1b_executable_filename");
    this->l1b_search_path = p.read_scalar<bool> ("l1b_search_path", false);
    this->l1b_buffer_nsamples = p.read_scalar<int> ("l1b_buffer_nsamples", 0);
    this->l1b_pipe_timeout = p.read_scalar<double> ("l1b_pipe_timeout", 0.0);
    this->l1b_pipe_blocking = p.read_scalar<bool> ("l1b_pipe_blocking", false);
    this->track_global_trigger_max = p.read_scalar<bool> ("track_global_trigger_max", false);

    // Lots of sanity checks.
    // First check that we have a consistent 'nstreams'.

    if ((ipaddr.size() == 1) && (port.size() > 1))
	this->ipaddr = vector<string> (port.size(), ipaddr[0]);
    else if ((ipaddr.size() > 1) && (port.size() == 1))
	this->port = vector<int> (ipaddr.size(), port[0]);
    
    if (ipaddr.size() != port.size())
	throw runtime_error(l1_config_filename + ": expected 'ip_addr' and 'port' to be lists of equal length");

    this->nstreams = ipaddr.size();

    // A few more checks..

    if (nbeams <= 0)
	throw runtime_error(l1_config_filename + ": 'nbeams' must be >= 1");
    if (nstreams <= 0)
	throw runtime_error(l1_config_filename + ": 'ip_addr' and 'port' must have length >= 1");
    if (rpc_address.size() != (unsigned int)nstreams)
	throw runtime_error(l1_config_filename + ": 'rpc_address' must be a list whose length is the number of (ip_addr,port) pairs");
    if (l1b_executable_filename.size() == 0)
	throw runtime_error(l1_config_filename + ": l1b_executable_filename must be a nonempty string");
    if (l1b_buffer_nsamples < 0)
	throw runtime_error(l1_config_filename + ": l1b_buffer_nsamples must be >= 0");
    if (l1b_pipe_timeout < 0.0)
	throw runtime_error(l1_config_filename + ": l1b_pipe_timeout must be >= 0.0");
    if (nbeams % nstreams) {
	throw runtime_error(l1_config_filename + ": nbeams (=" + to_string(nbeams) + ") must be a multiple of nstreams (="
			    + to_string(nstreams) + ", inferred from number of (ipaddr,port) pairs");
    }

    // Read beam_ids (postponed to here, so we get the check on 'nbeams')
    
    this->beam_ids = p.read_vector<int> ("beam_ids", vrange(0,nbeams));

    if (beam_ids.size() != nbeams)
	throw runtime_error(l1_config_filename + ": 'beam_ids' must have length 'nbeams'");
    
    // Now decide whether instance is "subscale" or "full-scale".

    // Factor 2 is from hyperthreading.
    int num_cores = std::thread::hardware_concurrency() / 2;

    if (nbeams <= 4)
	this->is_subscale = true;
    else if ((nbeams == 16) && (num_cores == 20))
	this->is_subscale = false;
    else {
	cerr << "ch-frb-l1: The L1 server can currently run in two modes: either a \"full-scale\" mode\n"
	     << "  with 16 beams and 20 cores, or a \"subscale\" mode with 4 beams and no core-pinning.\n"
	     << "  This appears to be an instance with " << nbeams << " beams, and " << num_cores << " cores.\n";
	exit(1);
    }

    // Final checks...

    bool unused_params_are_fatal = !this->fflag;
    p.check_for_unused_params(unused_params_are_fatal);

    if ((l1b_buffer_nsamples == 0) && (l1b_pipe_timeout <= 1.0e-6))
	die_unless_fflag_set("should specify either l1b_buffer_nsamples > 0, or l1b_pipe_timeout > 0.0, see MANUAL.md for discussion.");

    if (is_subscale && (bonsai_config.nfreq > 4096))
	die_unless_fflag_set("subscale instance with > 4096 frequency channels, presumably unintentional?");

    if (is_subscale && !slow_kernels)
	die_unless_fflag_set("subscale instance with slow_kernels=false, presumably unintentional?");

    if ((bonsai_config.nfreq > 4096) && slow_kernels)
	die_unless_fflag_set("nfreq > 4096 and slow_kernels=true, presumably unintentional?");
}


void l1_params::die_unless_fflag_set(const string &msg) const
{
    if (!this->fflag)
	throw runtime_error("ch-frb-l1: " + msg + "  To override this warning, use -f.");
    else if (this->l1_verbosity >= 1)
	cerr << "ch-frb-l1: warning: " << msg << "  Running anyway since -f was specified.\n";
}


// -------------------------------------------------------------------------------------------------
//
// make_input_stream(): returns a stream object which will read packets from the correlator.


static shared_ptr<ch_frb_io::intensity_network_stream> make_input_stream(const l1_params &config, int istream)
{
    assert(istream >= 0 && istream < config.nstreams);

    int nbeams = config.nbeams;
    int nstreams = config.nstreams;
    int nbeams_per_stream = xdiv(nbeams, nstreams);
    
    ch_frb_io::intensity_network_stream::initializer ini_params;

    ini_params.ipaddr = config.ipaddr[istream];
    ini_params.udp_port = config.port[istream];
    ini_params.beam_ids = vrange(istream * nbeams_per_stream, (istream+1) * nbeams_per_stream);
    ini_params.mandate_fast_kernels = !config.slow_kernels;
    ini_params.mandate_reference_kernels = config.slow_kernels;
    
    // Setting this flag means that an exception will be thrown if either:
    //
    //    1. the unassembled-packet ring buffer between the network and
    //       assembler threads is full (i.e. assembler thread is running slow)
    //
    //    2. the assembled_chunk ring buffer between the network and
    //       processing threads is full (i.e. processing thread is running slow)
    //
    // If we wanted, we could define separate flags for these two conditions.
    //
    // Note that in situation (2), the pipeline will crash anyway since
    // rf_pipelines doesn't contain code to handle gaps in the data.  This
    // is something that we'll fix soon, but it's nontrivial.
    
    ini_params.throw_exception_on_buffer_drop = true;

    // This disables the "telescoping" part of the telescoping ring buffers.
    // Currently, the telescoping logic is too slow for real-time use.  (The
    // symptom is that the assembler threads run slow, triggering condition (1)
    // from the previous comment.)  We should be able to fix this by writing
    // fancy assembly language kernels for the telescoping logic!

    ini_params.assembled_ringbuf_nlevels = 1;

    if (!config.is_subscale) {
	// Core-pinning logic for the full-scale L1 server.

	if (nstreams % 2 == 1)
	    throw runtime_error("ch-frb-l1: nstreams must be even, in order to divide dedispersion threads evenly between the two CPUs");

	// Note that processing threads 0-7 are pinned to cores 0-7 (on CPU1)
	// and cores 10-17 (on CPU2).  I decided to pin assembler threads to
	// cores 8 and 18.  This leaves cores 9 and 19 free for RPC and other IO.
	
	if (istream < (nstreams/2))
	    ini_params.assembler_thread_cores = {8,28};
	else
	    ini_params.assembler_thread_cores = {18,38};

	// I decided to pin all network threads to CPU1, since according to
	// the motherboard manual, all NIC's live on the same PCI-E bus as CPU1.
	//
	// I think it makes sense to avoid pinning network threads to specific
	// cores on the CPU, since they use minimal cycles, but scheduling latency
	// is important for minimizing packet drops.  I haven't really tested this
	// assumption though!

	ini_params.network_thread_cores = vconcat(vrange(0,10), vrange(20,30));
    }

    return ch_frb_io::intensity_network_stream::make(ini_params);
}


// -------------------------------------------------------------------------------------------------
//
// make_l1rpc_server()


static shared_ptr<L1RpcServer> make_l1rpc_server(const l1_params &config, int istream, shared_ptr<ch_frb_io::intensity_network_stream> stream) 
{
    assert(istream >= 0 && istream < config.nstreams);

    shared_ptr<L1RpcServer> rpc = make_shared<L1RpcServer>(stream, config.rpc_address[istream]);
    return rpc;
}


// -------------------------------------------------------------------------------------------------
//
// dedispersion_thread_main().
//
// Note: the 'ibeam' argument is an index satisfying 0 <= ibeam < config.nbeams, 
// where config.nbeams is the number of beams on the node.   Not a beam_id!

static void dedispersion_thread_main(const l1_params &config, const shared_ptr<ch_frb_io::intensity_network_stream> &sp, int ibeam)
{
    assert(ibeam >= 0 && ibeam < config.nbeams);

    try {
	const bonsai::config_params &bonsai_config = config.bonsai_config;

	vector<int> allowed_cores;
	if (!config.is_subscale) {
	    // Core-pinning logic for full-scale L1 server.
	    int c = (ibeam / 8) * 10 + (ibeam % 8);
	    allowed_cores = { c, c+20 };
	}

	// Pin thread before allocating anything.
	// Note that in the subscale case, 'allowed_cores' is an empty vector, and pin_thread_to_cores() no-ops.
	ch_frb_io::pin_thread_to_cores(allowed_cores);
	
	// Note: the distinction between 'ibeam' and 'beam_id' is a possible source of bugs!
	int beam_id = config.beam_ids[ibeam];
        auto stream = rf_pipelines::make_chime_network_stream(sp, beam_id);
	auto transform_chain = rf_pipelines::deserialize_transform_chain_from_json(config.rfi_transform_chain_json);

	bonsai::dedisperser::initializer ini_params;
	ini_params.verbosity = 0;

	auto dedisperser = make_shared<bonsai::dedisperser> (bonsai_config, ini_params);
	transform_chain.push_back(rf_pipelines::make_bonsai_dedisperser(dedisperser));

	// Trigger processors.
	shared_ptr<bonsai::trigger_pipe> l1b_trigger_pipe;
	shared_ptr<bonsai::global_max_tracker> max_tracker;

	if (config.track_global_trigger_max) {
	    max_tracker = make_shared<bonsai::global_max_tracker> ();
	    dedisperser->add_processor(max_tracker);
	}

	if (config.l1b_executable_filename.size() > 0) {
	    // Assumed L1b command line is: <l1_executable> <l1b_config> <beam_id>
	    vector<string> l1b_command_line = {
		config.l1b_executable_filename,
		config.l1b_config_filename,
		std::to_string(beam_id)
	    };

	    bonsai::trigger_pipe::initializer l1b_initializer;
	    l1b_initializer.timeout = config.l1b_pipe_timeout;
	    l1b_initializer.blocking = config.l1b_pipe_blocking;
	    l1b_initializer.search_path = config.l1b_search_path;
	    l1b_initializer.verbosity = config.l1b_pipe_io_debug ? 3: config.l1_verbosity;

	    if (config.l1b_buffer_nsamples > 0) {
		int nt_chunk = bonsai_config.nt_chunk;
		int nchunks = (config.l1b_buffer_nsamples + nt_chunk - 1) / nt_chunk;

		if ((config.l1_verbosity >= 1) && (config.l1b_buffer_nsamples != nchunks * nt_chunk)) {
		    cout << "ch-frb-l1: increasing l1b_buffer_nsamples: "
			 << config.l1b_buffer_nsamples << " -> " << (nchunks * nt_chunk) 
			 << " (rounding up to multiple of bonsai_config.nt_chunk)" << endl;
		}

		// Base capacity for config_params + a little extra for miscellaneous metadata...
		l1b_initializer.pipe_capacity = bonsai_config.serialize_to_buffer().size() + 1024;

		// ... plus capacity for coarse-grained triggers.
		for (int itree = 0; itree < bonsai_config.ntrees; itree++)
		    l1b_initializer.pipe_capacity += nchunks * bonsai_config.ntriggers_per_chunk[itree] * sizeof(float);

		if (config.l1_verbosity >= 2)
		    cout << "ch-frb-l1: l1b pipe_capacity will be " << l1b_initializer.pipe_capacity << " bytes" << endl;
	    }

	    // Important: pin L1b child process to same core as L1a parent thread.
	    // Note that in the subscale case, 'allowed_cores' is an empty vector, and the child process is not core-pinned.
	    l1b_initializer.child_cores = allowed_cores;

	    // The trigger_pipe constructor will spawn the L1b child process.
	    l1b_trigger_pipe = make_shared<bonsai::trigger_pipe> (l1b_command_line, l1b_initializer);
	    dedisperser->add_processor(l1b_trigger_pipe);
	}
	else if (config.l1_verbosity >= 1)
	    cout << "ch-frb-l1: config parameter 'l1b_executable_filename' is an empty string, L1b processes will not be spawned\n";

	// (transform_chain, outdir, json_output, verbosity)
	stream->run(transform_chain, string(), nullptr, 0);

	if (max_tracker) {
	    stringstream ss;
	    ss << "ch-frb-l1: beam_id=" << beam_id 
	       << ": most significant FRB has SNR=" << max_tracker->global_max_trigger
	       << ", and (dm,arrival_time)=(" << max_tracker->global_max_trigger_dm
	       << "," << max_tracker->global_max_trigger_arrival_time
	       << ")\n";
	    
	    cout << ss.str().c_str() << flush;
	}

	if (l1b_trigger_pipe) {
	    int l1b_status = l1b_trigger_pipe->wait_for_child();
	    if (config.l1_verbosity >= 1)
		cout << "l1b process exited with status " << l1b_status << endl;
	}
    } 
    catch (exception &e) {
	cerr << e.what() << "\n";
	throw;
    }
}


// -------------------------------------------------------------------------------------------------
//
// print_statistics()
//
// FIXME move equivalent functionality to ch_frb_io?


static void print_statistics(const l1_params &config, const vector<shared_ptr<ch_frb_io::intensity_network_stream>> &input_streams)
{
    assert((int)input_streams.size() == config.nstreams);

    for (int istream = 0; istream < config.nstreams; istream++) {
	cout << "stream " << istream << ": ipaddr=" << config.ipaddr[istream] << ", udp_port=" << config.port[istream] << endl;
 
	// vector<map<string,int>>
	auto statistics = input_streams[istream]->get_statistics();
	
	for (unsigned int irec = 0; irec < statistics.size(); irec++) {
	    cout << "    record " << irec  << endl;
	    const auto &s = statistics[irec];
	    
	    vector<string> keys;
	    for (const auto &kv: s)
		keys.push_back(kv.first);
	    
	    sort(keys.begin(), keys.end());
	    
	    for (const auto &k: keys) {
		auto kv = s.find(k);
		cout << "         " << k << " " << kv->second << endl;
	    }
	}
    }
}


// -------------------------------------------------------------------------------------------------


int main(int argc, char **argv)
{
    l1_params config(argc, argv);

    int nstreams = config.nstreams;
    int nbeams = config.nbeams;

    vector<shared_ptr<ch_frb_io::intensity_network_stream>> input_streams(nstreams);
    vector<shared_ptr<L1RpcServer> > rpc_servers(nbeams);
    vector<std::thread> threads(nbeams);

    for (int istream = 0; istream < nstreams; istream++)
	input_streams[istream] = make_input_stream(config, istream);

    for (int istream = 0; istream < nstreams; istream++) {
	rpc_servers[istream] = make_l1rpc_server(config, istream, input_streams[istream]);
        // returns std::thread
        rpc_servers[istream]->start();
    }

    for (int ibeam = 0; ibeam < nbeams; ibeam++) {
	cerr << "spawning thread " << ibeam << endl;
	int nbeams_per_stream = xdiv(nbeams, nstreams);
	int istream = ibeam / nbeams_per_stream;
	threads[ibeam] = std::thread(dedispersion_thread_main, config, input_streams[istream], ibeam);
    }

    for (int ibeam = 0; ibeam < nbeams; ibeam++)
	threads[ibeam].join();

    if (config.l1_verbosity >= 2)
	print_statistics(config, input_streams);

    return 0;
}
