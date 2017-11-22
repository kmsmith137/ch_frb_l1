#include <thread>
#include <fstream>

#include <ch_frb_io_internals.hpp>
#include <rf_pipelines.hpp>
#include <bonsai.hpp>
#include <l1-rpc.hpp>

#include "ch_frb_l1.hpp"
#include "chlog.hpp"

using namespace std;
using namespace ch_frb_l1;

static void usage()
{
    cerr << "Usage: packet-receiver [-fvmw] <l1_config.yaml>\n"
	 << "  -f forces the L1 server to run, even if the config files look fishy\n"
	 << "  -v increases verbosity of the toplevel logic\n"
	 << "  -m enables a very verbose debug trace of the memory_slab_pool allocation\n"
	 << "  -w enables a very verbose debug trace of the logic for writing chunks\n";
    exit(2);
}


// -------------------------------------------------------------------------------------------------

struct l1_params {
    l1_params(int argc, char **argv);

    // Command-line arguments
    string l1_config_filename;

    // Command-line flags
    // Currently, l1_verbosity can have the following values (I may add more later):
    //   1: pretty quiet
    //   2: pretty noisy

    bool fflag = false;
    int l1_verbosity = 1;
    bool memory_pool_debug = false;
    bool write_chunk_debug = false;

    // nstreams is automatically determined by the number of (ipaddr, port) pairs.
    // There will be one (network_thread, assembler_thread, rpc_server) triple for each stream.
    int nbeams = 0;
    int nfreq = 0;
    int nstreams = 0;
    int nt_per_packet = 0;
    int fpga_counts_per_sample = 384;

    // The L1 server can run in two modes: either a "production-scale" mode with 16 beams and 20 cores,
    // or a "subscale" mode with (nbeams <= 4) and no core-pinning.
    bool is_subscale = true;
    
    // If slow_kernels=false (the default), the L1 server will use fast assembly language kernels
    // for its packet processing.  If slow_kernels=true, then it will use reference kernels which
    // are much slower.
    //
    // Note 1: the slow kernels are too slow for non-subscale use!  If slow kernels are used on
    // the "production-scale" L1 server with (nbeams, nupfreq) = (16, 16), it may crash.
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

    // Buffer sizes, as specified in config file.
    int unassembled_ringbuf_nsamples = 4096;
    int assembled_ringbuf_nsamples = 8192;
    vector<int> telescoping_ringbuf_nsamples;
    double write_staging_area_gb = 0.0;
    
    // "Derived" unassembled ringbuf parameters.
    int unassembled_ringbuf_capacity = 0;
    int unassembled_nbytes_per_list = 0;
    int unassembled_npackets_per_list = 0;

    // "Derived" assembled and telescoping ringbuf parameters.
    int assembled_ringbuf_nchunks = 0;
    vector<int> telescoping_ringbuf_nchunks;

    // Parameters of the memory_slab_pool(s)
    int memory_slab_nbytes = 0;     // size (in bytes) of memory slab used to store assembled_chunk
    int memory_slabs_per_pool = 0;  // total memory slabs to allocate per cpu
    int npools = 0;

    // List of output devices (e.g. '/ssd', '/nfs')
    vector<string> output_devices;

    // Also intended for debugging.  If the optional parameter 'stream_filename_pattern'
    // is specified, then the L1 server will auto-stream all chunks to disk.  Warning:
    // it's very easy to use a lot of disk space this way!
    
    string stream_filename_pattern;
};


l1_params::l1_params(int argc, char **argv)
{
    const int nfreq_c = ch_frb_io::constants::nfreq_coarse_tot;

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
	    else if (argv[i][j] == 'f')
		this->fflag = true;
	    else if (argv[i][j] == 'm')
		this->memory_pool_debug = true;
	    else if (argv[i][j] == 'w')
		this->write_chunk_debug = true;
	    else
		usage();
	}
    }

    if (args.size() != 1)
	usage();

    this->l1_config_filename = args[0];

    // Remaining code in this function reads l1_config file.
    int yaml_verbosity = (this->l1_verbosity >= 2) ? 1 : 0;
    yaml_paramfile p(l1_config_filename, yaml_verbosity);

    // These parameters can be read right away.
    this->nbeams = p.read_scalar<int> ("nbeams");
    this->nfreq = p.read_scalar<int> ("nfreq");
    this->nt_per_packet = p.read_scalar<int> ("nt_per_packet");
    this->fpga_counts_per_sample = p.read_scalar<int> ("fpga_counts_per_sample", 384);
    this->ipaddr = p.read_vector<string> ("ipaddr");
    this->port = p.read_vector<int> ("port");
    this->rpc_address = p.read_vector<string> ("rpc_address");
    this->slow_kernels = p.read_scalar<bool> ("slow_kernels", false);
    this->unassembled_ringbuf_nsamples = p.read_scalar<int> ("unassembled_ringbuf_nsamples", 4096);
    this->assembled_ringbuf_nsamples = p.read_scalar<int> ("assembled_ringbuf_nsamples", 8192);
    this->telescoping_ringbuf_nsamples = p.read_vector<int> ("telescoping_ringbuf_nsamples", {});
    this->write_staging_area_gb = p.read_scalar<double> ("write_staging_area_gb", 0.0);
    this->output_devices = p.read_vector<string> ("output_devices");
    this->stream_filename_pattern = p.read_scalar<string> ("stream_filename_pattern", "");

    // Lots of sanity checks.
    // First check that we have a consistent 'nstreams'.

    if ((ipaddr.size() == 1) && (port.size() > 1))
	this->ipaddr = vector<string> (port.size(), ipaddr[0]);
    else if ((ipaddr.size() > 1) && (port.size() == 1))
	this->port = vector<int> (ipaddr.size(), port[0]);
    
    if (ipaddr.size() != port.size())
	throw runtime_error(l1_config_filename + ": expected 'ip_addr' and 'port' to be lists of equal length");

    this->nstreams = ipaddr.size();

    // More sanity checks..

    if (nbeams <= 0)
	throw runtime_error(l1_config_filename + ": 'nbeams' must be >= 1");
    if (nfreq <= 0)
	throw runtime_error(l1_config_filename + ": 'nfreq' must be >= 1");
    if (nfreq % nfreq_c)
	throw runtime_error(l1_config_filename + ": 'nfreq' must be a multiple of " + to_string(nfreq_c));
    if (nfreq > 16384)
	throw runtime_error(l1_config_filename + ": nfreq > 16384 is currently not allowed");
    if (nstreams <= 0)
	throw runtime_error(l1_config_filename + ": 'ip_addr' and 'port' must have length >= 1");
    if (nt_per_packet <= 0)
	throw runtime_error(l1_config_filename + ": 'nt_per_packet' must be >= 1");
    if (fpga_counts_per_sample <= 0)
	throw runtime_error(l1_config_filename + ": 'fpga_counts_per_sample' must be >= 1");
    if (rpc_address.size() != (unsigned int)nstreams)
	throw runtime_error(l1_config_filename + ": 'rpc_address' must be a list whose length is the number of (ip_addr,port) pairs");
    if (!slow_kernels && (nt_per_packet != 16))
	throw runtime_error(l1_config_filename + ": fast kernels (slow_kernels=false) currently require nt_per_packet=16");
    if (!slow_kernels && (nfreq % (2*nfreq_c)))
	throw runtime_error(l1_config_filename + ": fast kernels (slow_kernels=false) currently require nfreq divisible by " + to_string(2*nfreq_c));
    if (unassembled_ringbuf_nsamples <= 0)
	throw runtime_error(l1_config_filename + ": 'unassembled_ringbuf_nsamples' must be >= 1");	
    if (assembled_ringbuf_nsamples <= 0)
	throw runtime_error(l1_config_filename + ": 'assembled_ringbuf_nsamples' must be >= 1");
    if (telescoping_ringbuf_nsamples.size() > 4)
	throw runtime_error(l1_config_filename + ": 'telescoping_ringbuf_nsamples' must be a list of length <= 4");
    if ((telescoping_ringbuf_nsamples.size() > 0) && (telescoping_ringbuf_nsamples[0] < assembled_ringbuf_nsamples))
	throw runtime_error(l1_config_filename + ": if specified, 'telescoping_ringbuf_nsamples[0]' must be >= assembled_ringbuf_nsamples");
    if (write_staging_area_gb < 0.0)
	throw runtime_error(l1_config_filename + ": 'write_staging_area_gb' must be >= 0.0");

    for (unsigned int i = 0; i < telescoping_ringbuf_nsamples.size(); i++) {
	if (telescoping_ringbuf_nsamples[i] <= 0)
	    throw runtime_error(l1_config_filename + ": all elements of 'telescoping_ringbuf_nsamples' must be > 0");
    }

    if (nbeams % nstreams != 0) {
	throw runtime_error(l1_config_filename + ": nbeams (=" + to_string(nbeams) + ") must be a multiple of nstreams (="
			    + to_string(nstreams) + ", inferred from number of (ipaddr,port) pairs");
    }

    // Read beam_ids (postponed to here, so we get the check on 'nbeams' first).
    
    this->beam_ids = p.read_vector<int> ("beam_ids", vrange(0,nbeams));

    cout << "Expecting beam ids: ";
    for (int i=0; i<this->beam_ids.size(); i++) {
        cout << this->beam_ids[i] << ", ";
    }
    cout << endl;

    if (beam_ids.size() != (unsigned)nbeams)
	throw runtime_error(l1_config_filename + ": 'beam_ids' must have length 'nbeams'");

    // Decide whether instance is "subscale" or "production-scale".

    // Factor 2 is from hyperthreading.
    int num_cores = std::thread::hardware_concurrency() / 2;

    if (nbeams <= 4)
	this->is_subscale = true;
    else if ((nbeams == 16) && (num_cores == 20))
	this->is_subscale = false;
    else {
	cerr << "ch-frb-l1: The L1 server can currently run in two modes: either a \"production-scale\" mode\n"
	     << "  with 16 beams and 20 cores, or a \"subscale\" mode with 4 beams and no core-pinning.\n"
	     << "  This appears to be an instance with " << nbeams << " beams, and " << num_cores << " cores.\n";
	exit(1);
    }

    if (!is_subscale && (nstreams % 2 == 1))
	throw runtime_error(l1_config_filename + ": production-scale L1 server instances must have an odd number of streams");

    // "Derived" unassembled ringbuf params.

    int fp = nt_per_packet * fpga_counts_per_sample;   // FPGA counts per packet
    int np = int(40000/fp) + 1;                        // number of packets in 100 ms, rounded up.  This will correspond to one unassembled packet list.
    int nb = ch_frb_io::intensity_packet::packet_size(nbeams/nstreams, 1, nfreq/nfreq_c, nt_per_packet);

    this->unassembled_ringbuf_capacity = int(unassembled_ringbuf_nsamples / (np * nt_per_packet)) + 1;
    this->unassembled_nbytes_per_list = np * nfreq_c * nb;
    this->unassembled_npackets_per_list = np * nfreq_c;

    if (l1_verbosity >= 2) {
	cout << l1_config_filename << ": setting unassembled_ringbuf_capacity=" << unassembled_ringbuf_capacity << endl
	     << l1_config_filename << ": setting unassembled_nbytes_per_list=" << unassembled_nbytes_per_list << endl
	     << l1_config_filename << ": setting unassembled_npackets_per_list=" << unassembled_npackets_per_list << endl;
    }

    // "Derived" assembled and telescoping ringbuf params.

    int nt_c = ch_frb_io::constants::nt_per_assembled_chunk;
    this->assembled_ringbuf_nchunks = (assembled_ringbuf_nsamples + nt_c - 1) / nt_c;
    this->assembled_ringbuf_nchunks = max(assembled_ringbuf_nchunks, 2);

    if ((l1_verbosity >= 2) && (assembled_ringbuf_nsamples != assembled_ringbuf_nchunks * nt_c)) {
	cout << l1_config_filename << ": assembled_ringbuf_nsamples increased from " 
	     << assembled_ringbuf_nsamples << " to " << (assembled_ringbuf_nchunks * nt_c) 
	     << " (rounding up to multiple of ch_frb_io::nt_per_assembled_chunk)" << endl;
    }

    int nr = telescoping_ringbuf_nsamples.size();
    this->telescoping_ringbuf_nchunks.resize(nr);

    for (int i = 0; i < nr; i++) {
	nt_c = (1 << i) * ch_frb_io::constants::nt_per_assembled_chunk;
	this->telescoping_ringbuf_nchunks[i] = (telescoping_ringbuf_nsamples[i] + nt_c - 1) / nt_c;
	this->telescoping_ringbuf_nchunks[i] = max(telescoping_ringbuf_nchunks[i], 2);

	if ((l1_verbosity >= 2) && (telescoping_ringbuf_nsamples[i] != telescoping_ringbuf_nchunks[i] * nt_c)) {
	    cout << l1_config_filename << ": telescoping_ringbuf_nsamples[" << i << "] increased from "
		 << telescoping_ringbuf_nsamples[i] << " to " << (telescoping_ringbuf_nchunks[i] * nt_c)
		 << " (rounding up to multiple of ch_frb_io::nt_per_assembled_chunk)" << endl;
	}
    }

    // Memory slab parameters (npools, memory_slab_nbytes, memory_slabs_per_pool)
    //
    // The total memory usage consists of
    //   - live_chunks_per_beam (active + assembled_ringbuf + telescoping_ringbuf)
    //   - temporary_chunks_per_stream (temporaries in assembled_chunk::_put_assembled_chunk())
    //   - staging_chunks_per_pool (derived from config param 'write_staging_area_gb')

    int nupfreq = xdiv(nfreq, nfreq_c);
    this->memory_slab_nbytes = ch_frb_io::assembled_chunk::get_memory_slab_size(nupfreq, nt_per_packet);
    this->npools = is_subscale ? 1 : 2;

    int live_chunks_per_beam = 2;   // "active" chunks
    live_chunks_per_beam += assembled_ringbuf_nchunks;  // assembled_ringbuf
    
    // telescoping_ringbuf
    for (unsigned int i = 0; i < telescoping_ringbuf_nchunks.size(); i++)
	live_chunks_per_beam += telescoping_ringbuf_nchunks[i];

    // probably overkill, but this fudge factor accounts for the fact that the dedispersion 
    // thread can briefly hang on to a reference to the assembled_chunk.
    int fudge_factor = 4;
    if (telescoping_ringbuf_nchunks.size() > 0)
	fudge_factor = max(4 - telescoping_ringbuf_nchunks[0], 0);

    live_chunks_per_beam += fudge_factor;

    int temporary_chunks_per_stream = max(1, (int)telescoping_ringbuf_nchunks.size());
    int staging_chunks_per_pool = pow(2,30.) * write_staging_area_gb / (npools * memory_slab_nbytes);

    assert(nbeams % npools == 0);
    assert(nstreams % npools == 0);

    this->memory_slabs_per_pool = ((nbeams/npools) * live_chunks_per_beam 
				   + (nstreams/npools) * temporary_chunks_per_stream 
				   + staging_chunks_per_pool);

    if (l1_verbosity >= 1) {
	double gb = npools * memory_slabs_per_pool * double(memory_slab_nbytes) / pow(2.,30.);

	cout << "Total assembled_chunk memory on node: " << gb << " GB"
	     << " (chunk counts: " << nbeams << "*" << live_chunks_per_beam
	     << " + " << nstreams << "*" << temporary_chunks_per_stream
	     << " + " << npools << "*" << staging_chunks_per_pool << ")" << endl;
    }

    // Warnings that can be overridden with -f.

    bool have_warnings = false;

    if (!p.check_for_unused_params(false))  // fatal=false
	have_warnings = true;

    if (have_warnings) {
	if (this->fflag)
	    cout << "ch-frb-l1: the above warnings will be ignored, since the -f flag was specified." << endl;
	else {
	    cout << "ch-frb-l1: the above warning(s) are treated as fatal.  To force the L1 server to run anyway, use ch-frb-l1 -f." << endl;
	    exit(1);
	}
    }
}


// -------------------------------------------------------------------------------------------------
//
// make_output_device()


static shared_ptr<ch_frb_io::output_device> make_output_device(const l1_params &config, int idev)
{
    ch_frb_io::output_device::initializer ini_params;
    ini_params.device_name = config.output_devices[idev];
    ini_params.verbosity = config.write_chunk_debug ? 3 : config.l1_verbosity;

    if (!config.is_subscale) {
	// On the real CHIME nodes, all disk controllers and NIC's are on CPU1!
	// (See p. 1-10 of the motherboard manual.)  Therefore, we pin our I/O
	// threads to core 9 on CPU1.  (This core currently isn't used for anything
	// except I/O.)

	ini_params.io_thread_allowed_cores = {9, 29};
    }

    return ch_frb_io::output_device::make(ini_params);
}


// -------------------------------------------------------------------------------------------------
//
// make_memory_pool()


static shared_ptr<ch_frb_io::memory_slab_pool> make_memory_pool(const l1_params &config, int ipool)
{
    vector<int> allocation_cores;
    int verbosity = config.memory_pool_debug ? 2 : 1;

    if (!config.is_subscale)
	allocation_cores = vrange(10*ipool, 10*(ipool+1));

    return make_shared<ch_frb_io::memory_slab_pool> (config.memory_slab_nbytes, config.memory_slabs_per_pool, allocation_cores, verbosity);
}


// -------------------------------------------------------------------------------------------------
//
// make_input_stream(): returns a stream object which will read packets from the correlator.


static shared_ptr<ch_frb_io::intensity_network_stream> make_input_stream(const l1_params &config, int istream, 
									 const shared_ptr<ch_frb_io::memory_slab_pool> &memory_pool,
									 const vector<shared_ptr<ch_frb_io::output_device>> &output_devices)
{
    assert(istream >= 0 && istream < config.nstreams);
    assert(config.beam_ids.size() == (unsigned)config.nbeams);

    int nbeams = config.nbeams;
    int nstreams = config.nstreams;
    int nbeams_per_stream = xdiv(nbeams, nstreams);

    auto beam_id0 = config.beam_ids.begin() + istream * nbeams_per_stream;
    auto beam_id1 = config.beam_ids.begin() + (istream+1) * nbeams_per_stream;

    ch_frb_io::intensity_network_stream::initializer ini_params;

    ini_params.ipaddr = config.ipaddr[istream];
    ini_params.udp_port = config.port[istream];
    ini_params.beam_ids = vector<int> (beam_id0, beam_id1);
    ini_params.nupfreq = xdiv(config.nfreq, ch_frb_io::constants::nfreq_coarse_tot);
    ini_params.nt_per_packet = config.nt_per_packet;
    ini_params.fpga_counts_per_sample = config.fpga_counts_per_sample;
    ini_params.stream_id = istream + 1;   // +1 here since first NFS mount is /frb-archive-1, not /frb-archive-0
    ini_params.force_fast_kernels = !config.slow_kernels;
    ini_params.force_reference_kernels = config.slow_kernels;
    ini_params.unassembled_ringbuf_capacity = config.unassembled_ringbuf_capacity;
    ini_params.max_unassembled_packets_per_list = config.unassembled_npackets_per_list;
    ini_params.max_unassembled_nbytes_per_list = config.unassembled_nbytes_per_list;
    ini_params.assembled_ringbuf_capacity = config.assembled_ringbuf_nchunks;
    ini_params.telescoping_ringbuf_capacity = config.telescoping_ringbuf_nchunks;
    ini_params.memory_pool = memory_pool;
    ini_params.output_devices = output_devices;
    
    ini_params.throw_exception_on_beam_id_mismatch = false;

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

    if (!config.is_subscale) {
	// Core-pinning logic for the production-scale L1 server.

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

    auto ret = ch_frb_io::intensity_network_stream::make(ini_params);

    // If config.stream_filename_pattern is an empty string, then stream_to_files() doesn't do anything.
    ret->stream_to_files(config.stream_filename_pattern, 0);

    return ret;
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
// print_statistics()
//
// FIXME move equivalent functionality to ch_frb_io?


static void print_statistics(const l1_params &config, const vector<shared_ptr<ch_frb_io::intensity_network_stream>> &input_streams, int verbosity)
{
    assert((int)input_streams.size() == config.nstreams);

    if (verbosity <= 0)
	return;

    for (int istream = 0; istream < config.nstreams; istream++) {
	cout << "stream " << istream << ": ipaddr=" << config.ipaddr[istream] << ", udp_port=" << config.port[istream] << endl;
 
	// vector<map<string,int>>
	auto statistics = input_streams[istream]->get_statistics();

	if (verbosity <= 1) {
	    int count_packets_good = statistics[0]["count_packets_good"];
	    double assembler_thread_waiting_usec = statistics[0]["assembler_thread_waiting_usec"];
	    double assembler_thread_working_usec = statistics[0]["assembler_thread_working_usec"];
	    double assembler_thread_workfrac = assembler_thread_working_usec / (assembler_thread_waiting_usec + assembler_thread_working_usec);
	    
	    cout << "    count_packets_good: " << count_packets_good << endl;
	    cout << "    assembler_thread_workfrac: " << assembler_thread_workfrac << endl;
	    continue;
	}
	
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

static void read_thread_main(const l1_params &config, const shared_ptr<ch_frb_io::intensity_network_stream> &stream, int ibeam)
{
    //int assembler_id = ibeam;
    int assembler_id = -1;
    int beam_id = ibeam;
    const vector<int> &beam_ids = stream->ini_params.beam_ids;
    cout << "Read thread for beam index " << ibeam << " starting" << endl;
    for (unsigned int i = 0; i < beam_ids.size(); i++) {
        cout << "Stream_ini's beam id: " << i << endl;
        if (beam_ids[i] == beam_id) {
            assembler_id = i;
            break;
        }
    }
    if (assembler_id < 0) {
        //throw runtime_error("beam_id=" + stringify(beam_id)
        //+ " not found in stream beam_id list " + stringify(beam_ids));
        //throw runtime_error("Beam id " + std::to_string(beam_id) + " not found in beam_id list");
        cout << "Beam id " + std::to_string(beam_id) + " not found in beam_id list; quitting" << endl;
        return;
    }

    // tells network thread to start reading packets, returns immediately
    stream->start_stream();

    for (;;) {
        shared_ptr<ch_frb_io::assembled_chunk> chunk;
        //cout << "Reading assembled chunk from beam " << ibeam << endl;
        chunk = stream->get_assembled_chunk(assembler_id);
	if (!chunk)
            cout << "Got NULL chunk -- exiting" << endl;
        cout << "Read assembled chunk from beam index " << ibeam << ", chunk " << chunk->ichunk << ", chunk beam " << chunk->beam_id << endl;
	//chunk->decode(intensity, weights, istride, wstride);
	//chunk.reset();
    }
    
}
// -------------------------------------------------------------------------------------------------


int main(int argc, char **argv)
{
    l1_params config(argc, argv);

    int ndevices = config.output_devices.size();
    int npools = config.npools;
    int nstreams = config.nstreams;
    int nbeams = config.nbeams;

    assert(nstreams % npools == 0);
    assert(nbeams % nstreams == 0);

    vector<shared_ptr<bonsai::trigger_pipe>> l1b_subprocesses(nbeams);
    vector<shared_ptr<ch_frb_io::output_device>> output_devices(ndevices);
    vector<shared_ptr<ch_frb_io::memory_slab_pool>> memory_pools(npools);
    vector<shared_ptr<ch_frb_io::intensity_network_stream>> input_streams(nstreams);
    vector<shared_ptr<L1RpcServer> > rpc_servers(nstreams);
    vector<std::thread> rpc_threads(nstreams);

    vector<std::thread> read_threads(nbeams);

    for (int idev = 0; idev < ndevices; idev++)
	output_devices[idev] = make_output_device(config, idev);

    for (int ipool = 0; ipool < npools; ipool++)
	memory_pools[ipool] = make_memory_pool(config, ipool);

    for (int istream = 0; istream < nstreams; istream++) {
	int ipool = istream / (nstreams / npools);
	input_streams[istream] = make_input_stream(config, istream, memory_pools[ipool], output_devices);
    }

    for (int istream = 0; istream < nstreams; istream++) {
	rpc_servers[istream] = make_l1rpc_server(config, istream, input_streams[istream]);
        rpc_threads[istream] = rpc_servers[istream]->start();
    }

    for (int ibeam = 0; ibeam < nbeams; ibeam++) {
	int nbeams_per_stream = xdiv(nbeams, nstreams);
	int istream = ibeam / nbeams_per_stream;

	if (config.l1_verbosity >= 2) {
	    cout << "ch-frb-l1: spawning chunk-reading thread " << ibeam << ", beam_id=" << config.beam_ids[ibeam] 
		 << ", stream=" << config.ipaddr[istream] << ":" << config.port[istream] << endl;
	}

        read_threads[ibeam] = std::thread(read_thread_main, config, input_streams[istream], ibeam);
    }
    
    if (!config.is_subscale)
	cout << "ch-frb-l1: server is now running, but you will want to wait ~60 seconds before sending packets, or it may crash!  This will be fixed soon..." << endl;

    for (int ibeam = 0; ibeam < nbeams; ibeam++)
	read_threads[ibeam].join();

    for (int idev = 0; idev < ndevices; idev++) {
	output_devices[idev]->end_stream(true);   // wait=true
	output_devices[idev]->join_thread();
    }

    cout << "All write requests written, all i/o threads joined, shutting down RPC servers..." << endl;

    for (int istream = 0; istream < nstreams; istream++) {
	rpc_servers[istream]->do_shutdown();
	rpc_threads[istream].join();
    }
    
    print_statistics(config, input_streams, config.l1_verbosity);

    return 0;
}
