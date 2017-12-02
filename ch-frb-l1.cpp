// Major features missing:
//
//   - Distributed logging is not integrated
//   - If anything goes wrong, the L1 server will crash!
//
// The L1 server can run in two modes: either a "production-scale" mode with 20 cores and either 8 or 16 beams,
// or a "subscale" mode with (nbeams <= 4) and no core-pinning.
//
// The "production-scale" mode is hardcoded to assume the NUMA setup of the CHIMEFRB L1 nodes:
//   - Dual CPU
//   - 10 cores/cpu
//   - Hyperthreading enabled
//   - all NIC's on the same PCI-E bus as the first CPU
//   - all SSD's on the first CPU.
//
// Note that the Linux scheduler defines 40 "cores":
//   cores 0-9:    primary hyperthread on CPU1 
//   cores 10-19:  primary hyperthread on CPU2
//   cores 20-29:  secondary hyperthread on CPU1
//   cores 30-39:  secondary hyperthread on CPU2


#include <thread>
#include <fstream>
#include <iomanip>

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
    cerr << "Usage: ch-frb-l1 [-fvpmct] <l1_config.yaml> <rfi_config.json> <bonsai_config.hdf5> <l1b_config_file>\n"
	 << "  -f forces the L1 server to run, even if the config files look fishy\n"
	 << "  -v increases verbosity of the toplevel ch-frb-l1 logic\n"
	 << "  -p enables a very verbose debug trace of the pipe I/O between L1a and L1b\n"
	 << "  -m enables a very verbose debug trace of the memory_slab_pool allocation\n"
	 << "  -w enables a very verbose debug trace of the logic for writing chunks\n"
	 << "  -c deliberately crash dedispersion thread (for debugging, obviously)\n"
	 << "  -t starts a \"toy server\" which assembles packets, but does not run RFI removal,\n"
	 << "     dedispersion, or L1B (if -t is specified, then the last 3 arguments are optional)\n";

    exit(2);
}



// -------------------------------------------------------------------------------------------------
//
// l1_config: reads and parses config files, does a lot of sanity checking,
// but does not allocate any "heavyweight" data structures.


struct l1_config
{
    l1_config() { }
    l1_config(int argc, char **argv);

    // Command-line arguments
    string l1_config_filename;
    string rfi_config_filename;
    string bonsai_config_filename;
    string l1b_config_filename;

    Json::Value rfi_transform_chain_json;
    bonsai::config_params bonsai_config;

    // Command-line flags
    // Currently, l1_verbosity can have the following values (I may add more later):
    //   1: pretty quiet
    //   2: pretty noisy

    bool tflag = false;
    bool fflag = false;
    bool l1b_pipe_io_debug = false;
    bool memory_pool_debug = false;
    bool write_chunk_debug = false;
    bool deliberately_crash = false;
    int l1_verbosity = 1;

    // nstreams is automatically determined by the number of (ipaddr, port) pairs.
    // There will be one (network_thread, assembler_thread, rpc_server) triple for each stream.
    int nbeams = 0;
    int nfreq = 0;
    int nstreams = 0;
    int nt_per_packet = 0;
    int fpga_counts_per_sample = 384;
    
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
    int nbytes_per_memory_slab = 0;     // size (in bytes) of memory slab used to store assembled_chunk
    int total_memory_slabs = 0;         // total for node

    // List of output devices (e.g. '/ssd', '/nfs')
    vector<string> output_device_names;

    // L1b linkage.  Note: assumed L1b command line is:
    //   <l1b_executable_filename> <l1b_config_filename> <beam_id>

    string l1b_executable_filename;
    bool l1b_search_path = false;     // will $PATH be searched for executable?
    int l1b_buffer_nsamples = 0;      // determines buffer size between L1a and L1b (0 = system default)
    double l1b_pipe_timeout = 0.0;    // timeout in seconds between L1a and L1
    bool l1b_pipe_blocking = false;   // setting this to true is equivalent to a very large l1b_pipe_timeout

    // "Derived" parameter: L1b pipe capacity (derived from l1b_buffer_nsamples)
    int l1b_pipe_capacity = 0;

    // Forces RFI removal and dedispersion to run in separate threads (shouldn't
    // need to specify this explicitly, except for debugging).
    bool force_asynchronous_dedispersion = false;

    // Occasionally useful for debugging: If track_global_trigger_max is true, then when 
    // the L1 server exits, it will print the (DM, arrival time) of the most significant FRB.
    bool track_global_trigger_max = false;

    // Also intended for debugging.  If the optional parameter 'stream_acqname' is
    // specified, then the L1 server will auto-stream all chunks to disk.  Warning:
    // it's very easy to use a lot of disk space this way!

    string stream_acqname;            // specified in config file
    string stream_filename_pattern;   // derived from 'stream_acqname'

    void _have_warnings() const;
};


// FIXME: split this monster constructor into multiple functions for readability?
l1_config::l1_config(int argc, char **argv)
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
	    else if (argv[i][j] == 'p')
		this->l1b_pipe_io_debug = true;
	    else if (argv[i][j] == 'm')
		this->memory_pool_debug = true;
	    else if (argv[i][j] == 'w')
		this->write_chunk_debug = true;
	    else if (argv[i][j] == 'c')
		this->deliberately_crash = true;
	    else if (argv[i][j] == 't')
		this->tflag = true;
	    else
		usage();
	}
    }

    if (args.size() == 4) {
	this->l1_config_filename = args[0];
	this->rfi_config_filename = args[1];
	this->bonsai_config_filename = args[2];
	this->l1b_config_filename = args[3];
    }
    else if (tflag && (args.size() == 1))
	this->l1_config_filename = args[0];
    else
	usage();


    if (!tflag) {
	// Open rfi_config file.
	std::ifstream rfi_config_file(rfi_config_filename);
	if (rfi_config_file.fail())
	    throw runtime_error("ch-frb-l1: couldn't open file " + rfi_config_filename);

	// Parse rfi_config file and initialize 'rfi_transform_chain_json'.
	Json::Reader rfi_config_reader;
	if (!rfi_config_reader.parse(rfi_config_file, this->rfi_transform_chain_json))
	    throw runtime_error("ch-frb-l1: couldn't parse json file " + rfi_config_filename);
	
	// Throwaway call, to get an early check that rfi_config_file is valid.
	// FIXME bind() here?
	auto rfi_chain = rf_pipelines::pipeline_object::from_json(rfi_transform_chain_json);

#if 0
	// FIXME pretty-print rfi_chain
	if (l1_verbosity >= 2) {
	    cout << rfi_config_filename << ": " << rfi_chain.size() << " transforms\n";
	    for (unsigned int i = 0; i < rfi_chain.size(); i++)
		cout << rfi_config_filename << ": transform " << i << "/" << rfi_chain.size() << ": " << rfi_chain[i]->name << "\n";
	}
#endif

	// Parse bonsai_config file and initialize 'bonsai_config'.
	this->bonsai_config = bonsai::config_params(bonsai_config_filename);

	if (l1_verbosity >= 2) {
	    bool write_derived_params = true;
	    string prefix = bonsai_config_filename + ": ";
	    bonsai_config.write(cout, write_derived_params, prefix);
	}

	// Check that the bonsai config file contains all transfer matrices.
	// This will be the case if it is the output of 'bonsai-mkweight'.
	
	bool have_transfer_matrices = true;

	for (int itree = 0; itree < bonsai_config.ntrees; itree++)
	    if (!bonsai_config.transfer_matrices[itree])
		have_transfer_matrices = false;
	
	if (!have_transfer_matrices) {
	    throw runtime_error(bonsai_config_filename + ": transfer matrices not found.  Maybe you accidentally specified a .txt file"
				+ " instead of .hdf5?  See ch_frb_l1/MANUAL.md for more info");
	}
    }

    // Remaining code in this function reads l1_config yaml file.

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
    this->output_device_names = p.read_vector<string> ("output_devices");
    this->l1b_executable_filename = tflag ? p.read_scalar<string> ("l1b_executable_filename","") : p.read_scalar<string> ("l1b_executable_filename");
    this->l1b_search_path = p.read_scalar<bool> ("l1b_search_path", false);
    this->l1b_buffer_nsamples = p.read_scalar<int> ("l1b_buffer_nsamples", 0);
    this->l1b_pipe_timeout = p.read_scalar<double> ("l1b_pipe_timeout", 0.0);
    this->l1b_pipe_blocking = p.read_scalar<bool> ("l1b_pipe_blocking", false);
    this->force_asynchronous_dedispersion = p.read_scalar<bool> ("force_asynchronous_dedispersion", false);
    this->track_global_trigger_max = p.read_scalar<bool> ("track_global_trigger_max", false);
    this->stream_acqname = p.read_scalar<string> ("stream_acqname", "");

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
    if (!tflag && (nfreq != bonsai_config.nfreq))
	throw runtime_error("ch-frb-l1: 'nfreq' values in l1 config file and bonsai config file must match");
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
    if (l1b_buffer_nsamples < 0)
	throw runtime_error(l1_config_filename + ": l1b_buffer_nsamples must be >= 0");
    if (l1b_pipe_timeout < 0.0)
	throw runtime_error(l1_config_filename + ": l1b_pipe_timeout must be >= 0.0");
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

    if (beam_ids.size() != (unsigned)nbeams)
	throw runtime_error(l1_config_filename + ": 'beam_ids' must have length 'nbeams'");

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

    // Memory slab parameters (nbytes_per_memory_slab, total_memory_slabs)
    //
    // The total memory usage consists of
    //   - live_chunks_per_beam (active + assembled_ringbuf + telescoping_ringbuf)
    //   - temporary_chunks_per_stream (temporaries in assembled_chunk::_put_assembled_chunk())
    //   - total_staging_chunks (derived from config param 'write_staging_area_gb')

    int nupfreq = xdiv(nfreq, nfreq_c);
    this->nbytes_per_memory_slab = ch_frb_io::assembled_chunk::get_memory_slab_size(nupfreq, nt_per_packet);

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
    int total_staging_chunks = pow(2,30.) * write_staging_area_gb / nbytes_per_memory_slab;

    this->total_memory_slabs = nbeams * live_chunks_per_beam + nstreams * temporary_chunks_per_stream + total_staging_chunks;

    if (l1_verbosity >= 1) {
	double gb = total_memory_slabs * double(nbytes_per_memory_slab) / pow(2.,30.);

	cout << "Total assembled_chunk memory on node: " << gb << " GB"
	     << " (chunk counts: " << nbeams << "*" << live_chunks_per_beam
	     << " + " << nstreams << "*" << temporary_chunks_per_stream
	     << " + " << total_staging_chunks << ")" << endl;
    }

    // l1b_pipe_capacity

    if (!tflag && (l1b_buffer_nsamples > 0)) {
	int nt_chunk = bonsai_config.nt_chunk;
	int nchunks = (l1b_buffer_nsamples + nt_chunk - 1) / nt_chunk;

	if ((l1_verbosity >= 2) && (l1b_buffer_nsamples != nchunks * nt_chunk)) {
	    cout << l1_config_filename << ": increasing l1b_buffer_nsamples: "
		 << l1b_buffer_nsamples << " -> " << (nchunks * nt_chunk) 
		 << " (rounding up to multiple of bonsai_nt_chunk)" << endl;
	}

	// Base capacity for config_params + a little extra for miscellaneous metadata...
	this->l1b_pipe_capacity = bonsai_config.serialize_to_buffer().size() + 1024;

	// ... plus capacity for coarse-grained triggers.
	for (int itree = 0; itree < bonsai_config.ntrees; itree++)
	    this->l1b_pipe_capacity += nchunks * bonsai_config.ntriggers_per_chunk[itree] * sizeof(float);

	if (l1_verbosity >= 2)
	    cout << l1_config_filename << ": l1b pipe_capacity will be " << l1b_pipe_capacity << " bytes" << endl;
    }

    // Warnings that can be overridden with -f.

    bool have_warnings = false;

    if (!p.check_for_unused_params(false))  // fatal=false
	have_warnings = true;

    if ((l1b_executable_filename.size() > 0) && (l1b_buffer_nsamples == 0) && (l1b_pipe_timeout <= 1.0e-6)) {
	cout << l1_config_filename << ": should specify either l1b_buffer_nsamples > 0, or l1b_pipe_timeout > 0.0, see MANUAL.md for discussion." << endl;
	have_warnings = true;
    }

    if (!tflag && (bonsai_config.nfreq > 4096) && slow_kernels) {
	cout << l1_config_filename << ": nfreq > 4096 and slow_kernels=true, presumably unintentional?" << endl;
	have_warnings = true;
    }

    if (have_warnings)
	_have_warnings();

    // I put this last, since it creates directories.
    this->stream_filename_pattern = ch_frb_l1::acqname_to_filename_pattern(stream_acqname, beam_ids, "/local/acq_data");
    cout << "XXX stream_filename_pattern = " << stream_filename_pattern << endl;
}


void l1_config::_have_warnings() const
{
    if (this->fflag) {
	cout << "ch-frb-l1: the above warning(s) will be ignored, since the -f flag was specified." << endl;
	return;
    }
    
    cout << "ch-frb-l1: the above warning(s) are treated as fatal.  To force the L1 server to run anyway, use ch-frb-l1 -f." << endl;
    exit(1);
}


// -------------------------------------------------------------------------------------------------
//
// Dedispersion thread context and main().
//
// Note: the 'ibeam' argument is an index satisfying 0 <= ibeam < config.nbeams, 
// where config.nbeams is the number of beams on the node.   Not a beam_id!
//
// Note: the 'l1b_subprocess' argument can be an empty pointer (in the case
// where the L1 server is run without L1B).


struct dedispersion_thread_context {
    l1_config config;
    shared_ptr<ch_frb_io::intensity_network_stream> sp;
    shared_ptr<bonsai::trigger_pipe> l1b_subprocess;   // warning: can be empty pointer!
    vector<int> allowed_cores;
    bool asynchronous_dedispersion;   // run RFI and dedispersion in separate threads?
    int ibeam;

    void _thread_main() const;
    void _toy_thread_main() const;  // if -t command-line argument is specified
};


// Note: only called if config.tflag == false.
void dedispersion_thread_context::_thread_main() const
{
    assert(!config.tflag);
    assert(ibeam >= 0 && ibeam < config.nbeams);

    // Pin thread before allocating anything.
    ch_frb_io::pin_thread_to_cores(allowed_cores);

    // Note: deep copy here, to get thread-local copy of transfer matrices!
    bonsai::config_params bonsai_config = config.bonsai_config.deep_copy();
    
    // Note: the distinction between 'ibeam' and 'beam_id' is a possible source of bugs!
    int beam_id = config.beam_ids[ibeam];
    auto stream = rf_pipelines::make_chime_network_stream(sp, beam_id);
    auto rfi_chain = rf_pipelines::pipeline_object::from_json(config.rfi_transform_chain_json);

    bonsai::dedisperser::initializer ini_params;
    ini_params.fill_rfi_mask = true;                   // very important for real-time analysis!
    ini_params.analytic_variance_on_the_fly = false;   // prevent accidental initialization from non-hdf5 config file (should have been checked already, but another check can't hurt)
    ini_params.allocate = true;                        // redundant, but I like making it explicit
    ini_params.verbosity = 0;

    if (asynchronous_dedispersion) {
	ini_params.asynchronous = true;
	ini_params.async_allowed_cores = allowed_cores;  // should be redundant, since bonsai worker thread inherits cpu affinity of parent thread by default
    }

    auto dedisperser = make_shared<bonsai::dedisperser> (bonsai_config, ini_params);  // not config.bonsai_config

    // Trigger processors.

    shared_ptr<bonsai::global_max_tracker> max_tracker;

    if (config.track_global_trigger_max) {
	max_tracker = make_shared<bonsai::global_max_tracker> ();
	dedisperser->add_processor(max_tracker);
    }

    if (l1b_subprocess)
	dedisperser->add_processor(l1b_subprocess);

    auto bonsai_transform = rf_pipelines::make_bonsai_dedisperser(dedisperser);
	
    auto pipeline = make_shared<rf_pipelines::pipeline> ();
    pipeline->add(stream);
    pipeline->add(rfi_chain);
    pipeline->add(bonsai_transform);

    rf_pipelines::run_params rparams;
    rparams.outdir = "";  // disables
    rparams.verbosity = 0;

    // FIXME more sensible synchronization scheme!
    pipeline->run(rparams);
    
    if (max_tracker) {
	stringstream ss;
	ss << "ch-frb-l1: beam_id=" << beam_id 
	   << ": most significant FRB has SNR=" << max_tracker->global_max_trigger
	   << ", and (dm,arrival_time)=(" << max_tracker->global_max_trigger_dm
	   << "," << max_tracker->global_max_trigger_arrival_time
	   << ")\n";
	
	cout << ss.str().c_str() << flush;
    }

    // FIXME is it necessary for the dedispersion thread to wait for L1B?
    // If not, it would be clearer to move this into l1_server::join_all_threads().

    if (l1b_subprocess) {
	int l1b_status = l1b_subprocess->wait_for_child();
	if (config.l1_verbosity >= 1)
	    cout << "l1b process exited with status " << l1b_status << endl;
    }
} 


// Note: Called if config.tflag == false.
void dedispersion_thread_context::_toy_thread_main() const
{
    assert(config.tflag);
    assert(ibeam >= 0 && ibeam < config.nbeams);

    ch_frb_io::pin_thread_to_cores(allowed_cores);

    // FIXME: beam_id stuff is more confusing than it needs to be!  This will be simplified soon.
    int nbeams = config.nbeams;
    int nstreams = config.nstreams;
    int nbeams_per_stream = xdiv(nbeams, nstreams);
    int ibeam_within_stream = ibeam % nbeams_per_stream;

    // FIXME: stream-starting stuff also needs cleanup.
    sp->start_stream();

    for (;;) {
        auto chunk = sp->get_assembled_chunk(ibeam_within_stream);

	// Some voodoo to reduce interleaved output
	usleep(ibeam * 10000);

        if (!chunk) {
	    cout << ("    [beam" + to_string(ibeam) + "]: got NULL chunk, exiting\n");
	    return;
	}

	if (ibeam_within_stream == 0) {
	    auto event_counts = sp->get_event_counts();

	    stringstream ss;

	    ss << sp->ini_params.ipaddr << ":" << sp->ini_params.udp_port << ": "
	       << " nrecv=" << event_counts[ch_frb_io::intensity_network_stream::packet_received]
	       << ", ngood=" << event_counts[ch_frb_io::intensity_network_stream::packet_good]
	       << ", nbad=" << event_counts[ch_frb_io::intensity_network_stream::packet_bad]
	       << ", ndropped=" << event_counts[ch_frb_io::intensity_network_stream::packet_dropped]
	       << ", ahit=" << event_counts[ch_frb_io::intensity_network_stream::assembler_hit]
	       << ", amiss=" << event_counts[ch_frb_io::intensity_network_stream::assembler_miss]
	       << "\n";

	    string s = ss.str();
	    cout << s.c_str();
	}

	if (config.l1_verbosity >= 2)
	    cout << ("    [beam" + to_string(ibeam) + "]: read chunk " + to_string(chunk->ichunk) + "\n");

        //chunk->decode(intensity, weights, istride, wstride);
        //chunk.reset();
    }
}


static void dedispersion_thread_main(const dedispersion_thread_context &context)
{
    try {
	if (context.config.tflag)
	    context._toy_thread_main();
	else
	    context._thread_main();
    }
    catch (exception &e) {
	cerr << e.what() << "\n";
	throw;
    }
}


// -------------------------------------------------------------------------------------------------
//
// This master data structure defines an L1 server instance.


struct l1_server {        
    using corelist_t = vector<int>;
    
    const l1_config config;
    
    int ncpus = 0;
    vector<corelist_t> cores_on_cpu;   // length-ncpus, defines mapping of cores to cpus
    
    // Core-pinning scheme
    corelist_t output_thread_cores;             // assumed same corelist for all I/O threads
    corelist_t network_thread_cores;            // assumed same for all network threads
    vector<corelist_t> dedispersion_cores;      // length config.nbeams, used for (RFI, dedisp, L1B).
    vector<corelist_t> assembler_thread_cores;  // length nstreams
    bool asynchronous_dedispersion = false;     // run RFI and dedispersion in separate threads?
    double sleep_hack = 0.0;                    // a temporary kludge that will go away soon

    // "Heavyweight" data structures.
    vector<shared_ptr<bonsai::trigger_pipe>> l1b_subprocesses;   // can be vector of empty pointers, if L1B is not being run.
    vector<shared_ptr<ch_frb_io::output_device>> output_devices;
    vector<shared_ptr<ch_frb_io::memory_slab_pool>> memory_slab_pools;
    vector<shared_ptr<ch_frb_io::intensity_network_stream>> input_streams;
    vector<shared_ptr<L1RpcServer>> rpc_servers;
    vector<std::thread> rpc_threads;
    vector<std::thread> dedispersion_threads;

    // The constructor reads the configuration files, does a lot of sanity checks,
    // but does not initialize any "heavyweight" data structures.
    l1_server(int argc, char **argv);

    // These methods incrementally construct the "heavyweight" data structures.
    void spawn_l1b_subprocesses();
    void make_output_devices();
    void make_memory_slab_pools();
    void make_input_streams();
    void make_rpc_servers();
    void spawn_dedispersion_threads();

    // These methods wait for the server to exit, and print some summary info.
    void join_all_threads();
    void print_statistics();

    // Helper methods called by constructor.
    void _init_subscale();
    void _init_20cores_16beams();
    void _init_20cores_8beams();
};


l1_server::l1_server(int argc, char **argv) :
    config(argc, argv)
{
    // Factor of 2 is from hyperthreading.
    int num_cores = std::thread::hardware_concurrency() / 2;

    this->dedispersion_cores.resize(config.nbeams);
    this->assembler_thread_cores.resize(config.nstreams);

    if (config.nbeams <= 4)
	_init_subscale();
    else if ((config.nbeams == 16) && (num_cores == 20))
	_init_20cores_16beams();
    else if ((config.nbeams == 8) && (num_cores == 20))
	_init_20cores_8beams();
    else {
	cerr << "ch-frb-l1: The L1 server can currently run in two modes: either a \"production-scale\" mode\n"
	     << "  with 20 cores and either 8 or 16 beams, or a \"subscale\" mode with 4 beams and no core-pinning.\n"
	     << "  This appears to be an instance with " << config.nbeams << " beams, and " << num_cores << " cores.\n";
	exit(1);
    }

    // Check that these members have been initialized by _init_xxx().
    assert(ncpus > 0);
    assert(cores_on_cpu.size() == size_t(ncpus));

    if (config.force_asynchronous_dedispersion)
	this->asynchronous_dedispersion = true;
}


void l1_server::_init_subscale()
{
    if (config.nfreq > 4096) {
	cout << config.l1_config_filename << ": subscale instance with > 4096 frequency channels, presumably unintentional?" << endl;
	config._have_warnings();
    }
    
    this->ncpus = 1;
    this->cores_on_cpu.resize(1);
}


void l1_server::_init_20cores_16beams()
{
    if (config.nstreams != 4)
	throw runtime_error("ch-frb-l1: in the \"production\" 16-beam case, we currently require 4 streams (this could be generalized pretty easily)");

    this->ncpus = 2;
    this->cores_on_cpu.resize(2);
    this->sleep_hack = config.tflag ? 5.0 : 30.0;

    // See comment at top of file.
    this->cores_on_cpu[0] = vconcat(vrange(0,10), vrange(20,30));
    this->cores_on_cpu[1] = vconcat(vrange(10,20), vrange(30,40));
    
    // On the real CHIME nodes, all disk controllers and NIC's are on CPU1!
    // (See p. 1-10 of the motherboard manual.)  Therefore, we pin output threads
    // threads to core 9 on CPU1.  (This core currently isn't used for anything else.)
    
    this->output_thread_cores = {9, 29};

    // Note that processing threads 0-7 are pinned to cores 0-7 (on CPU1)
    // and cores 10-17 (on CPU2).  I decided to pin assembler threads to
    // cores 8 and 18.  This leaves cores 9 and 19 free for RPC and other IO.

    this->assembler_thread_cores[0] = {8,28};
    this->assembler_thread_cores[1] = {8,28};
    this->assembler_thread_cores[2] = {18,38};
    this->assembler_thread_cores[3] = {18,38};

    // I decided to pin all network threads to CPU1, since according to
    // the motherboard manual, all NIC's live on the same PCI-E bus as CPU1.
    //
    // I think it makes sense to avoid pinning network threads to specific
    // cores on the CPU, since they use minimal cycles, but scheduling latency
    // is important for minimizing packet drops.  I haven't really tested this
    // assumption though!

    this->network_thread_cores = cores_on_cpu[0];

    // We pin dedispersion threads to cores 0-7, on both CPU's.
    // (16 dedispersion threads total)

    for (int i = 0; i < 2; i++)
	for (int j = 0; j < 8; j++)
	    this->dedispersion_cores[8*i+j] = { 10*i+j, 10*i+j+20 };
}


void l1_server::_init_20cores_8beams()
{
    if (config.nstreams != 2)
	throw runtime_error("ch-frb-l1: in the \"production\" 8-beam case, we currently require 2 streams (this could be generalized pretty easily)");

    // The following assignments are very similar to _init_20cores_16beams(), so we omit comments.

    this->ncpus = 2;
    this->cores_on_cpu.resize(2);
    this->cores_on_cpu[0] = vconcat(vrange(0,10), vrange(20,30));
    this->cores_on_cpu[1] = vconcat(vrange(10,20), vrange(30,40));    
    this->output_thread_cores = {9, 29};
    this->assembler_thread_cores[0] = {8,28};
    this->assembler_thread_cores[1] = {18,38};
    this->network_thread_cores = cores_on_cpu[0];
    this->sleep_hack = config.tflag ? 5.0 : 30.0;

    // These assignments differ from _init_20cores_8beams().
    
    this->asynchronous_dedispersion = true;

    for (int i = 0; i < 2; i++)
	for (int j = 0; j < 4; j++)
	    this->dedispersion_cores[4*i+j] = { 10*i+2*j, 10*i+2*j+1, 10*i+j+20, 10*i+j+21 };
}


void l1_server::spawn_l1b_subprocesses()
{
    if (l1b_subprocesses.size() != 0)
	throw("ch-frb-l1 internal error: double call to spawn_l1b_subprocesses()");
    
    this->l1b_subprocesses.resize(config.nbeams);

    if (config.tflag)
	return;  // 

    if (config.l1b_executable_filename.size() == 0) {
	if (config.l1_verbosity >= 1)
	    cout << "ch-frb-l1: config parameter 'l1b_executable_filename' is an empty string, L1b processes will not be spawned\n";
	return;  // note that 'l1b_subprocesses' gets initialized to a vector of empty pointers
    }

    for (int ibeam = 0; ibeam < config.nbeams; ibeam++) {
	// L1b command line is: <l1_executable> <l1b_config> <beam_id>
	vector<string> l1b_command_line = {
	    config.l1b_executable_filename,
	    config.l1b_config_filename,
	    std::to_string(config.beam_ids[ibeam])
	};
	
	bonsai::trigger_pipe::initializer l1b_initializer;
	l1b_initializer.timeout = config.l1b_pipe_timeout;
	l1b_initializer.blocking = config.l1b_pipe_blocking;
	l1b_initializer.search_path = config.l1b_search_path;
	l1b_initializer.verbosity = config.l1b_pipe_io_debug ? 3: config.l1_verbosity;
	l1b_initializer.pipe_capacity = config.l1b_pipe_capacity;
	l1b_initializer.child_cores = dedispersion_cores[ibeam];   // pin L1B child process to same cores as L1A parent thread.
    
	// The trigger_pipe constructor will spawn the L1b child process.
	this->l1b_subprocesses[ibeam] = make_shared<bonsai::trigger_pipe> (l1b_command_line, l1b_initializer);
    }
}


void l1_server::make_output_devices()
{
    if (output_devices.size() != 0)
	throw("ch-frb-l1 internal error: double call to make_output_devices()");

    int ndev = config.output_device_names.size();
    this->output_devices.resize(ndev);

    for (int idev = 0; idev < ndev; idev++) {
	ch_frb_io::output_device::initializer ini_params;
	ini_params.device_name = config.output_device_names[idev];
	ini_params.verbosity = config.write_chunk_debug ? 3 : config.l1_verbosity;
	ini_params.io_thread_allowed_cores = this->output_thread_cores;
	
	this->output_devices[idev] = ch_frb_io::output_device::make(ini_params);
    }
}


void l1_server::make_memory_slab_pools()
{
    if (memory_slab_pools.size() != 0)
	throw("ch-frb-l1 internal error: double call to make_output_devices()");
    
    int verbosity = config.memory_pool_debug ? 2 : 1;
    int memory_slabs_per_cpu = config.total_memory_slabs / ncpus;
    
    this->memory_slab_pools.resize(ncpus);
    
    for (int icpu = 0; icpu < ncpus; icpu++)
	memory_slab_pools[icpu] = make_shared<ch_frb_io::memory_slab_pool> (config.nbytes_per_memory_slab, memory_slabs_per_cpu, cores_on_cpu[icpu], verbosity);
}


void l1_server::make_input_streams()
{
    if (input_streams.size() != 0)
	throw("ch-frb-l1 internal error: double call to make_input_streams()");
    if (memory_slab_pools.size() != size_t(ncpus))
	throw("ch-frb-l1 internal error: make_input_streams() was called, without first calling make_memory_slab_pools()");
    if (output_devices.size() != config.output_device_names.size())
	throw("ch-frb-l1 internal error: make_input_streams() was called, without first calling make_output_devices()");
    
    int nstreams_per_cpu = xdiv(config.nstreams, ncpus);
    int nbeams_per_stream = xdiv(config.nbeams, config.nstreams);

    this->input_streams.resize(config.nstreams);

    for (int istream = 0; istream < config.nstreams; istream++) {
	auto beam_id0 = config.beam_ids.begin() + istream * nbeams_per_stream;
	auto beam_id1 = config.beam_ids.begin() + (istream+1) * nbeams_per_stream;
	auto memory_slab_pool = this->memory_slab_pools[istream / nstreams_per_cpu];
	
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
	ini_params.deliberately_crash = config.deliberately_crash;
	ini_params.assembler_thread_cores = this->assembler_thread_cores[istream];
	ini_params.network_thread_cores = this->network_thread_cores;
	ini_params.unassembled_ringbuf_capacity = config.unassembled_ringbuf_capacity;
	ini_params.max_unassembled_packets_per_list = config.unassembled_npackets_per_list;
	ini_params.max_unassembled_nbytes_per_list = config.unassembled_nbytes_per_list;
	ini_params.assembled_ringbuf_capacity = config.assembled_ringbuf_nchunks;
	ini_params.telescoping_ringbuf_capacity = config.telescoping_ringbuf_nchunks;
	ini_params.memory_pool = memory_slab_pool;
	ini_params.output_devices = this->output_devices;
	ini_params.sleep_hack = this->sleep_hack;
	
	// Setting the 'throw_exception_on_buffer_drop' flag means that an exception 
	// will be thrown if either:
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
	//
	// We currently set the flag in the "normal" case, but leave it un-set
	// for the "toy" server (ch-frb-l1 -t).

	ini_params.throw_exception_on_buffer_drop = !config.tflag;

	input_streams[istream] = ch_frb_io::intensity_network_stream::make(ini_params);
	
	// If config.stream_filename_pattern is an empty string, then stream_to_files() doesn't do anything.
	input_streams[istream]->stream_to_files(config.stream_filename_pattern, 0);   // (pattern, priority)
    }
}


void l1_server::make_rpc_servers()
{
    if (rpc_servers.size() || rpc_threads.size())
	throw("ch-frb-l1 internal error: double call to make_rpc_servers()");
    if (input_streams.size() != size_t(config.nstreams))
	throw("ch-frb-l1 internal error: make_rpc_servers() was called, without first calling make_input_streams()");

    this->rpc_servers.resize(config.nstreams);
    this->rpc_threads.resize(config.nstreams);
    
    for (int istream = 0; istream < config.nstreams; istream++) {
	rpc_servers[istream] = make_shared<L1RpcServer> (input_streams[istream], config.rpc_address[istream]);
	rpc_threads[istream] = rpc_servers[istream]->start();
    }
}

       
void l1_server::spawn_dedispersion_threads()
{
    if (dedispersion_threads.size())
	throw("ch-frb-l1 internal error: double call to spawn_dedispersion_threads()");
    if (input_streams.size() != size_t(config.nstreams))
	throw("ch-frb-l1 internal error: spawn_dedispersion_threads() was called, without first calling make_input_streams()");
    if (l1b_subprocesses.size() != size_t(config.nbeams))
	throw("ch-frb-l1 internal error: spawn_dedispersion_threads() was called, without first calling spawn_l1b_subprocesses()");

    this->dedispersion_threads.resize(config.nbeams);
    
    if (config.l1_verbosity >= 1)
	cout << "ch-frb-l1: spawning " << config.nbeams << " dedispersion thread(s)" << endl;

    for (int ibeam = 0; ibeam < config.nbeams; ibeam++) {
	int nbeams_per_stream = xdiv(config.nbeams, config.nstreams);
	int istream = ibeam / nbeams_per_stream;
    
	dedispersion_thread_context context;
	context.config = this->config;
	context.sp = this->input_streams[istream];
	context.l1b_subprocess = this->l1b_subprocesses[ibeam];
	context.allowed_cores = this->dedispersion_cores[ibeam];
	context.asynchronous_dedispersion = this->asynchronous_dedispersion;
	context.ibeam = ibeam;
	
	if (config.l1_verbosity >= 2) {
	    cout << "ch-frb-l1: spawning dedispersion thread " << ibeam << ", beam_id=" << config.beam_ids[ibeam] 
		 << ", stream=" << context.sp->ini_params.ipaddr << ":" << context.sp->ini_params.udp_port
		 << ", allowed_cores=" << ch_frb_io::vstr(context.allowed_cores) 
		 << ", asynchronous_dedispersion=" << context.asynchronous_dedispersion << endl;
	}

	dedispersion_threads[ibeam] = std::thread(dedispersion_thread_main, context);
    }
}


void l1_server::join_all_threads()
{
    for (size_t ibeam = 0; ibeam < dedispersion_threads.size(); ibeam++)
	dedispersion_threads[ibeam].join();
    
    cout << "All dedispersion threads joined, waiting for pending write requests..." << endl;

    for (size_t idev = 0; idev < output_devices.size(); idev++) {
	output_devices[idev]->end_stream(true);   // wait=true
	output_devices[idev]->join_thread();
    }

    cout << "All write requests written, shutting down RPC servers..." << endl;

    for (size_t istream = 0; istream < rpc_servers.size(); istream++)
	rpc_servers[istream]->do_shutdown();
    for (size_t istream = 0; istream < rpc_threads.size(); istream++)
	rpc_threads[istream].join();
}



// FIXME move equivalent functionality to ch_frb_io?
void l1_server::print_statistics()
{
    if (config.l1_verbosity <= 0)
	return;

    for (size_t istream = 0; istream < input_streams.size(); istream++) {
	cout << "stream " << istream << ": ipaddr=" << config.ipaddr[istream] << ", udp_port=" << config.port[istream] << endl;
 
	// vector<map<string,int>>
	auto statistics = input_streams[istream]->get_statistics();

	if (config.l1_verbosity <= 1) {
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


// -------------------------------------------------------------------------------------------------


int main(int argc, char **argv)
{
    l1_server server(argc, argv);

    server.spawn_l1b_subprocesses();
    server.make_output_devices();
    server.make_memory_slab_pools();
    server.make_input_streams();
    server.make_rpc_servers();
    server.spawn_dedispersion_threads();

    server.join_all_threads();
    server.print_statistics();

    return 0;
}
