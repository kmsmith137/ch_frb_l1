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

#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>

#include <curl/curl.h>

#include "ch_frb_l1.hpp"
#include "chlog.hpp"

#include "CLI11.hpp"

using namespace std;
using namespace ch_frb_l1;


typedef std::unique_lock<std::mutex> ulock;


static unordered_map<string, string> get_interface_map() {
    unordered_map<string, string> interfaces;
    // Get list of network interfaces...
    struct ifaddrs *ifaces, *iface;
    if (getifaddrs(&ifaces))
        throw runtime_error("Failed to get network interfaces -- getifaddrs(): " + string(strerror(errno)));
    for (iface = ifaces; iface; iface = iface->ifa_next) {
        struct sockaddr* address = iface->ifa_addr;
        if (address->sa_family != AF_INET)
            continue;
        struct sockaddr_in* inaddress = reinterpret_cast<struct sockaddr_in*>(address);
        struct in_addr addr = inaddress->sin_addr;
        char* addrstring = inet_ntoa(addr);
        chlog("Network interface: " << iface->ifa_name << " has IP " << addrstring);
        interfaces[string(iface->ifa_name)] = string(addrstring);
    }
    freeifaddrs(ifaces);
    return interfaces;
}

// "tcp://eno2:5555" -> "tcp://10.7.100.15:5555"
static string convert_zmq_address(const string& addr, const unordered_map<string, string>& interfaces) {
    size_t proto = addr.find("//");
    if (proto == std::string::npos)
        return addr;
    size_t port = addr.rfind(":");
    if (port == std::string::npos)
        return addr;
    string host = addr.substr(proto + 2, port - (proto+2));
    auto val = interfaces.find(host);
    if (val == interfaces.end())
        return addr;
    return addr.substr(0, proto+2) + val->second + addr.substr(port);
}

static string convert_ip_address(const string& orig_addr, const unordered_map<string, string>& interfaces) {
    // Grab the port off the end, if it exists
    string addr(orig_addr);
    size_t port = addr.rfind(":");
    string portstring;
    if (port != std::string::npos) {
        portstring = addr.substr(port);
	addr = addr.substr(0, port);
	//cout << "Pulled off port string: '" << portstring << "', cut addr string to '" << addr << "'" << endl;
    }
    // Try to parse as dotted-decimal IP address
    struct in_addr inaddr;
    if (inet_aton(addr.c_str(), &inaddr) == 1)
        // Correctly parsed as dotted-decimal IP address.
        return addr + portstring;
    // If doesn't parse as dotted-decimal, lookup in interfaces mapping.
    auto val = interfaces.find(addr);
    if (val == interfaces.end())
        throw runtime_error("Config file ipaddr entry \"" + orig_addr + "\" -> \"" + addr + "\" was not dotted IP address and was not one of the known network interfaces");
    //chlog("Mapped IP addr " << ipaddr[i] << " to " << val->second);
    return val->second + portstring;
}


// FIXME: split this monster constructor into multiple functions for readability?
l1_config::l1_config(int argc, const char **argv)
{
    const int nfreq_c = ch_frb_io::constants::nfreq_coarse_tot;

    vector<string> args;

    vector<int> acq_beams;
    string acq_name;
    bool acq_nfs;

    bool verbose = false;

    CLI::App parser{"ch-frb-l1 CHIME FRB L1 server"};
    parser.add_flag("-v,--verbose", verbose, "Increases verbosity of the toplevel ch-frb-l1 logic");
    parser.add_flag("-f,--force", this->fflag, "Forces the L1 server to run, even if the config files look fishy");
    parser.add_flag("-p,--pipe", this->l1b_pipe_io_debug, "Enables a very verbose debug trace of the pipe I/O between L1a and L1b");
    parser.add_flag("-i,--ignore", this->ignore_end_of_stream, "Ignores end-of-stream packets");
    parser.add_flag("-m,--memory", this->memory_pool_debug, "Enables a very verbose debug trace of the memory_slab_pool allocation");
    parser.add_flag("-w,--write", this->write_chunk_debug, "Eables a very verbose debug trace of the logic for writing chunks");
    parser.add_flag("-c,--crash", this->deliberately_crash, "Deliberately crash dedispersion thread (for debugging, obviously)");
    parser.add_flag("-t,--toy", this->tflag, "Starts a \"toy server\" which assembles packets, but does not run RFI removal, dedispersion, or L1B (if -t is specified, then the last 3 arguments are optional)");
    parser.add_flag("-r,--rfi", this->rflag, "Starts an \"RFI testing\" (semi-toy) server with no bonsai dedispersion or L1B (last 2 args are then optional).");
    /*
     parser.add_option("-a,--acq", acq_name, "Stream data to disk, saving it to this acquisition directory name");
     parser.add_flag("-n,--nfs", acq_nfs, "For streaming data acquisition, stream to NFS, not SSD");
     parser.add_option("-b,--beam", acq_beams, "For streaming data acquisition, beam number to capture (can be repeated; default is all beams)");
     */
    parser.add_option("l1_config", this->l1_config_filename, "l1_config.yaml")
        ->required()->check(CLI::ExistingFile);
    parser.add_option("rfi_config", this->rfi_config_filename, "rfi_config.json")
        ->check(CLI::ExistingFile);
    parser.add_option("bonsai_config", this->bonsai_config_filename, "bonsai_config.hdf5")
        ->check(CLI::ExistingFile);
    parser.add_option("l1b_config_file", this->l1b_config_filename, "L1b config file");
    //->check(CLI::ExistingFile);

    try {
        parser.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        parser.exit(e);
        exit(2);
    }

    if (verbose)
        this->l1_verbosity = 2;

    if (!tflag && !rflag && (
                             (this->rfi_config_filename.size() == 0) ||
                             (this->bonsai_config_filename.size() == 0) ||
                             (this->l1b_config_filename.size() == 0))) {
        cout << "Need rfi_config, bonsai_config, and l1b_config_filename." << endl;
        cout << "Run with --help for more details." << endl;
        exit(2);
    } else if (!tflag && rflag && (this->rfi_config_filename.size() == 0)) {
        cout << "Need rfi_config." << endl;
        cout << "Run with --help for more details." << endl;
        exit(2);
    }

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

	// Pretty-print rfi_chain
	if (l1_verbosity >= 2) {
	    cout << rfi_config_filename << ": transforms:\n";
            rf_pipelines::print_pipeline(rfi_chain);
        }
    }
    if (!tflag && !rflag) {
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
    this->nt_align = p.read_scalar<int> ("nt_align");
    this->nrfifreq = p.read_scalar<int> ("nrfifreq");
    this->ipaddr = p.read_vector<string> ("ipaddr");
    this->port = p.read_vector<int> ("port");
    this->rpc_address = p.read_vector<string> ("rpc_address");
    this->heavy_rpc_address = p.read_vector<string> ("heavy_rpc_address", vector<string>());
    this->prometheus_address = p.read_vector<string> ("prometheus_address");
    this->logger_address = p.read_scalar<string> ("logger_address", "");
    this->frame0_url = p.read_scalar<string> ("frame0_url");
    this->frame0_timeout = p.read_scalar<int> ("frame0_timeout_ms", 3000);
    this->rfi_mask_meas_history = p.read_scalar<int>("rfi_mask_meas_history", 300);
    this->slow_kernels = p.read_scalar<bool> ("slow_kernels", false);
    this->unassembled_ringbuf_nsamples = p.read_scalar<int> ("unassembled_ringbuf_nsamples", 4096);
    this->assembled_ringbuf_nsamples = p.read_scalar<int> ("assembled_ringbuf_nsamples", 8192);
    this->telescoping_ringbuf_nsamples = p.read_vector<int> ("telescoping_ringbuf_nsamples", {});
    this->write_staging_area_gb = p.read_scalar<double> ("write_staging_area_gb", 0.0);
    this->output_device_names = p.read_vector<string> ("output_devices");
    this->intensity_prescale = p.read_scalar<float> ("intensity_prescale", 1.0);
    this->l1b_executable_filename = tflag ? p.read_scalar<string> ("l1b_executable_filename","") : p.read_scalar<string> ("l1b_executable_filename");
    this->l1b_search_path = p.read_scalar<bool> ("l1b_search_path", false);
    this->l1b_buffer_nsamples = p.read_scalar<int> ("l1b_buffer_nsamples", 0);
    this->l1b_pipe_timeout = p.read_scalar<double> ("l1b_pipe_timeout", 0.0);
    this->l1b_pipe_blocking = p.read_scalar<bool> ("l1b_pipe_blocking", false);
    this->force_asynchronous_dedispersion = p.read_scalar<bool> ("force_asynchronous_dedispersion", false);
    this->track_global_trigger_max = p.read_scalar<bool> ("track_global_trigger_max", false);

    // Get the map from network interface names ("eno2") to IP address.
    unordered_map<string, string> interfaces = get_interface_map();

    // Convert network interface names in "ipaddr", eg, "eno2", into the interface's IP address.
    for (size_t i=0; i<ipaddr.size(); i++)
        ipaddr[i] = convert_ip_address(ipaddr[i], interfaces);

    // Convert network interface names in "rpc_address" entries.
    for (size_t i=0; i<rpc_address.size(); i++)
        // "tcp://eno2:5555" -> "tcp://10.7.100.15:5555"
        rpc_address[i] = convert_zmq_address(rpc_address[i], interfaces);

    // Convert network interface names in "rpc_address" entries.
    for (size_t i=0; i<heavy_rpc_address.size(); i++)
        // "tcp://eno2:5555" -> "tcp://10.7.100.15:5555"
        heavy_rpc_address[i] = convert_zmq_address(heavy_rpc_address[i], interfaces);
    
    // Convert network interface names in "prometheus_address" entries.
    for (size_t i=0; i<prometheus_address.size(); i++)
        prometheus_address[i] = convert_ip_address(prometheus_address[i], interfaces);

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
    if (nrfifreq < 0)
	throw runtime_error(l1_config_filename + ": 'nrfifreq' must be positive (or zero), and must match the number of downsampled frequencies in the RFI chain JSON file -- probably 1024.");
    if (!tflag && !rflag && (nfreq != bonsai_config.nfreq))
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
    if ((nt_align < 0) || (nt_align % ch_frb_io::constants::nt_per_assembled_chunk))
	throw runtime_error(l1_config_filename + ": 'nt_align' must be a multiple of " + to_string(ch_frb_io::constants::nt_per_assembled_chunk));
    if (rpc_address.size() != (unsigned int)nstreams)
	throw runtime_error(l1_config_filename + ": 'rpc_address' must be a list whose length is the number of (ip_addr,port) pairs");
    if (heavy_rpc_address.size() &&
        (heavy_rpc_address.size() != (unsigned int)nstreams))
	throw runtime_error(l1_config_filename + ": 'heavy_rpc_address', if specified, must be a list whose length is the number of (ip_addr,port) pairs");
    if (prometheus_address.size() != (unsigned int)nstreams)
	throw runtime_error(l1_config_filename + ": 'prometheus_address' must be a list whose length is the number of (ip_addr,port) pairs");
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

    // Read stream params (postponed to here, so we get 'beam_ids' first).

    /*
     // If a stream is specified on the command-line, override the config file.
     if (acq_name.size()) {
        if (acq_name == "none") {
            // no streaming!
        } else {
            this->stream_devname = acq_nfs ? "nfs" : "ssd";
            this->stream_acqname = acq_name;
            this->stream_beam_ids = acq_beams;
        }
    } else {
        this->stream_devname = p.read_scalar<string> ("stream_devname", "ssd");
        this->stream_acqname = p.read_scalar<string> ("stream_acqname", "");
        this->stream_beam_ids = p.read_vector<int> ("stream_beam_ids", this->beam_ids);
    }

    for (int b: stream_beam_ids)
	if (!vcontains(beam_ids, b))
	    throw runtime_error(l1_config_filename + ": 'stream_beam_ids' must be a subset of 'beam_ids' (which defaults to [0,...,nbeams-1] if unspecified)");
     */

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
    this->nbytes_per_memory_slab = ch_frb_io::assembled_chunk::get_memory_slab_size(nupfreq, nt_per_packet, this->nrfifreq);

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

    if (!tflag && !rflag && (l1b_buffer_nsamples > 0)) {
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

    if (nrfifreq == 0) {
        cout << "Warning: nrfifreq was set to zero (or not set) -- RFI masks will not be saved in callback data!" << endl;
	have_warnings = true;
    }
    
    if ((l1b_executable_filename.size() > 0) && (l1b_buffer_nsamples == 0) && (l1b_pipe_timeout <= 1.0e-6)) {
	cout << l1_config_filename << ": should specify either l1b_buffer_nsamples > 0, or l1b_pipe_timeout > 0.0, see MANUAL.md for discussion." << endl;
	have_warnings = true;
    }

    if (!tflag && !rflag && (bonsai_config.nfreq > 4096) && slow_kernels) {
	cout << l1_config_filename << ": nfreq > 4096 and slow_kernels=true, presumably unintentional?" << endl;
	have_warnings = true;
    }

    if (have_warnings)
	_have_warnings();

    // I put this last, since it creates directories.
    this->stream_filename_pattern = ch_frb_l1::acqname_to_filename_pattern(stream_devname, stream_acqname, vrange(1,nstreams+1), stream_beam_ids);
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

class ch_frb_l1::stream_coordinator {
public:
    stream_coordinator(int nbeams) :
        checkedin(nbeams, false),
        proceed(false),
        nreturned(0)
    {
    }
    bool wait_for(int index) {
        ulock u(mutx);
        chlog("stream_coord: waiting: " << index);
        checkedin[index] = true;
        bool alldone = true;
        for (bool r : checkedin) {
            if (!r) {
                alldone = false;
                break;
            }
        }
        if (alldone) {
            chlog("stream_coord: all beams have arrived!");
            nreturned++;
            return true;
        }
        while (!proceed)
            cond.wait(u);
        nreturned++;
        if (nreturned == checkedin.size()) {
            for (size_t i=0; i<checkedin.size(); i++)
                checkedin[i] = false;
            proceed = false;
            nreturned = 0;
        }
        return false;
    }
    void release() {
        proceed = true;
        cond.notify_all();
    }
    /*
      void reset() {
      ulock u(mutx);
      for (size_t i=0; i<checkedin.size(); i++)
      checkedin[i] = false;
      proceed = false;
      }
    */
protected:
    std::mutex mutx;
    std::condition_variable cond;
    std::vector<bool> checkedin;
    bool proceed;
    int nreturned;
};





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
    shared_ptr<stream_coordinator> reset_coord;
    shared_ptr<mask_stats_map> ms_map;
    std::function<void(int, shared_ptr<const bonsai::dedisperser>,
                       shared_ptr<const rf_pipelines::pipeline_object> latency_pre,
                       shared_ptr<const rf_pipelines::pipeline_object> latency_post
                       )> set_bonsai;
    shared_ptr<bonsai::trigger_pipe> l1b_subprocess;   // warning: can be empty pointer!
    shared_ptr<rf_pipelines::intensity_injector> injector_transform;
    vector<int> allowed_cores;
    bool asynchronous_dedispersion;   // run RFI and dedispersion in separate threads?
    int ibeam;
    int stream_ibeam;  // aka iassembler

    void _thread_main() const;
    void _toy_thread_main() const;  // if -t command-line argument is specified

    // Helper function called in _thread_main(), during initialization
    void _init_mask_counters(const shared_ptr<rf_pipelines::pipeline_object> &pipeline, int beam_id) const;
};


void dedispersion_thread_context::_init_mask_counters(const shared_ptr<rf_pipelines::pipeline_object> &pipeline, int stream_ibeam) const
{
    vector<shared_ptr<rf_pipelines::mask_counter_transform>> mask_counters;
    bool clippers_after_mask_counters = false;

    // This lambda-function is passed to rf_pipelines::visit_pipeline(), 
    // to find the mask_counters, and test whether clippers occur after mask_counters.

    auto find_mask_counters = [&mask_counters, &clippers_after_mask_counters]
	(const shared_ptr<rf_pipelines::pipeline_object> &p, int depth)
    {
	// A transform is considered to be a clipper if its class_name contains the substring "clipper".
	// FIXME: "feels" like a hack, is there a better criterion?
	bool is_clipper = (p->class_name.find("clipper") != string::npos);

	if (is_clipper) {
	    if (mask_counters.size() > 0)
		clippers_after_mask_counters = true;
	    return;
	}
	    
	auto counter = dynamic_pointer_cast<rf_pipelines::mask_counter_transform> (p);

	if (counter) {
	    // cout << "Found mask counter: " << counter->where << endl;
	    clippers_after_mask_counters = false;
	    mask_counters.push_back(counter);
	}
    };
    
    rf_pipelines::visit_pipeline(find_mask_counters, pipeline);

    if (config.nrfifreq > 0) {
	if (mask_counters.size() == 0)
	    throw runtime_error("ch-frb-l1: RFI masks requested, but no mask_counters in the RFI config JSON file");
	if (clippers_after_mask_counters)
	    throw runtime_error("ch-frb-l1: RFI masks requested, and clippers occur after the last mask_counter in the RFI config JSON file");
    }

    chlog("Setting up " << mask_counters.size() << " mask counters");

    for (unsigned int i = 0; i < mask_counters.size(); i++) {
	rf_pipelines::mask_counter_transform::runtime_attrs attrs;
	attrs.ringbuf_nhistory = config.rfi_mask_meas_history;
	attrs.chime_stream_index = stream_ibeam;

	// If RFI masks are requested, then the last mask_counter in the chain fills assembled_chunks.
	if ((config.nrfifreq > 0) && ((i+1) == mask_counters.size()))
	    attrs.chime_stream = this->sp;

	mask_counters[i]->set_runtime_attrs(attrs);
	this->ms_map->put(stream_ibeam, mask_counters[i]->where, mask_counters[i]->ringbuf);
    }
}


// Note: only called if config.tflag == false.
void dedispersion_thread_context::_thread_main() const
{
    assert(!config.tflag);
    assert(ibeam >= 0 && ibeam < config.nbeams);

    // Pin thread before allocating anything.
    ch_frb_io::pin_thread_to_cores(allowed_cores);

    bonsai::config_params bonsai_config;
    if (!config.rflag)
        // Note: deep copy here, to get thread-local copy of transfer matrices!
        bonsai_config = config.bonsai_config.deep_copy();
    
    // Note: the distinction between 'ibeam' and 'stream_ibeam' is a possible source of bugs!
    auto stream = rf_pipelines::make_chime_network_stream(sp, stream_ibeam, config.intensity_prescale);
    auto rfi_chain = rf_pipelines::pipeline_object::from_json(config.rfi_transform_chain_json);

    shared_ptr<rf_pipelines::wi_transform> bonsai_transform;
    shared_ptr<bonsai::dedisperser> dedisperser;
    shared_ptr<bonsai::global_max_tracker> max_tracker;

    if (!config.rflag) {
        bonsai::dedisperser::initializer ini_params;
        ini_params.fill_rfi_mask = true;                   // very important for real-time analysis!
        ini_params.analytic_variance_on_the_fly = false;   // prevent accidental initialization from non-hdf5 config file (should have been checked already, but another check can't hurt)
        ini_params.allocate = true;                        // redundant, but I like making it explicit
        ini_params.verbosity = 0;

        if (asynchronous_dedispersion) {
            ini_params.asynchronous = true;

            // The following line is now commented out.
            //
            //   ini_params.async_allowed_cores = allowed_cores;
            //
            // Previously, I was including this, even though it should be redundant given the call to pin_thread_to_cores() above.
            // However, it appears that this is not safe, since std::hardware_concurreny() sometimes returns 1 after the first call
            // to pin_thread_to_cores().  It is very strange that this only happens sometimes!  But commenting out the line above
            // seems to fix it.
        }

        dedisperser = make_shared<bonsai::dedisperser> (bonsai_config, ini_params);  // not config.bonsai_config

        // Trigger processors.

        if (config.track_global_trigger_max) {
            max_tracker = make_shared<bonsai::global_max_tracker> ();
            dedisperser->add_processor(max_tracker);
        }

        if (l1b_subprocess)
            dedisperser->add_processor(l1b_subprocess);

        bonsai_transform = rf_pipelines::make_bonsai_dedisperser(dedisperser);
    }

    _init_mask_counters(rfi_chain, stream_ibeam);
        
    auto pipeline = make_shared<rf_pipelines::pipeline> ();
    pipeline->add(stream);
    pipeline->add(injector_transform);
    pipeline->add(rfi_chain);
    if (!config.rflag)
        pipeline->add(bonsai_transform);

    // Find pipeline stages to use for latency monitoring: 'stream' input, and
    // the last step in the RFI chain
    shared_ptr<rf_pipelines::pipeline_object> latency1 = stream;
    shared_ptr<rf_pipelines::pipeline_object> latency2;
    auto find_last_transform = [&latency2]
	(const shared_ptr<rf_pipelines::pipeline_object> &p, int depth) {
        latency2 = p;
    };
    rf_pipelines::visit_pipeline(find_last_transform, rfi_chain);
    set_bonsai(ibeam, dedisperser, latency1, latency2);

    rf_pipelines::run_params rparams;
    rparams.outdir = "";  // disables
    rparams.verbosity = 0;

    for (;;) {
        // FIXME more sensible synchronization scheme!
        pipeline->run(rparams);
        if (!config.mflag)
            break;
        chlog("Multi-stream: pipeline finished.  Resetting!");
        if (reset_coord->wait_for(stream_ibeam)) {
            //if (stream_ibeam == 0) {
            chlog("ch-frb-l1: I am stream_ibeam " << stream_ibeam << ": resetting underlying stream!");
            sp->reset_stream();
            /// HACK -- ch-frb-simulate-l0 sends 5 end-of-stream packets, separated by 0.1 sec --
            // so we'll wait a full second and then flush them!
            sleep(1);
            sp->flush_end_of_stream();
            reset_coord->release();
        }
        chlog("ch-frb-l1: reset coordination finished (" << stream_ibeam << ")!");

        if (max_tracker) {
            stringstream ss;
            ss << "ch-frb-l1: beam index=" << ibeam
               << ": most significant FRB has SNR=" << max_tracker->global_max_trigger
               << ", and (dm,arrival_time)=(" << max_tracker->global_max_trigger_dm
               << "," << max_tracker->global_max_trigger_arrival_time
               << ")\n";
            cout << ss.str().c_str() << flush;
        }

        // FIXME is it necessary for the dedispersion thread to wait for L1B?
        // If not, it would be clearer to move this into l1_server::join_all_threads().
        if (l1b_subprocess) {
            chlog("Waiting for L1b process to finish...");
            int l1b_status = l1b_subprocess->wait_for_child();
            if (config.l1_verbosity >= 1)
                cout << "l1b process exited with status " << l1b_status << endl;
            chlog("L1b process finished.");
        }
        
        chlog("Restarting pipeline!");
        
        pipeline->reset();
    }
    // shut it down!!
    sp->join_threads();
    
} 


// Note: Called if config.tflag == true.
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

    sp->wait_for_first_packet();

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
    ch_frb_io::chime_log_set_thread_name("Bonsai-" + std::to_string(context.ibeam));
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




l1_server::l1_server(int argc, const char **argv) :
    config(argc, argv)
{
    command_line = "";
    for (int i=0; i<argc; i++)
        command_line += string(i ? " " : "") + string(argv[i]);

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

    if (config.tflag || config.rflag)
	return;  // 

    if (config.l1b_executable_filename.size() == 0) {
	if (config.l1_verbosity >= 1)
	    cout << "ch-frb-l1: config parameter 'l1b_executable_filename' is an empty string, L1b processes will not be spawned\n";
	return;  // note that 'l1b_subprocesses' gets initialized to a vector of empty pointers
    }

    for (int ibeam = 0; ibeam < config.nbeams; ibeam++) {
	// L1b command line is: <l1_executable> <l1b_config> [no <beam_id>]
	vector<string> l1b_command_line = {
	    config.l1b_executable_filename,
	    config.l1b_config_filename
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


void l1_server::start_logging()
{
    chlog("Opening chlog socket.");
    ch_frb_io::chime_log_open_socket();
    if (config.logger_address.size()) {
        chlog("Logging to " << config.logger_address);
        ch_frb_io::chime_log_add_server(config.logger_address);
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
	throw("ch-frb-l1 internal error: double call to make_memory_slab_pools()");
    
    int verbosity = config.memory_pool_debug ? 2 : 1;
    int memory_slabs_per_cpu = config.total_memory_slabs / ncpus;
    
    this->memory_slab_pools.resize(ncpus);
    
    for (int icpu = 0; icpu < ncpus; icpu++)
	memory_slab_pools[icpu] = make_shared<ch_frb_io::memory_slab_pool> (config.nbytes_per_memory_slab, memory_slabs_per_cpu, cores_on_cpu[icpu], verbosity);
}


void l1_server::first_packet_received(vector<int> beams) {
    chlog("First packet received.  Beams:");
    for (int b : beams) {
        chlog("  beam " << b);
    }
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
    this->stream_reset_coordinators.resize(config.nstreams);

    for (int istream = 0; istream < config.nstreams; istream++) {
	auto memory_slab_pool = this->memory_slab_pools[istream / nstreams_per_cpu];
	
	ch_frb_io::intensity_network_stream::initializer ini_params;

	ch_frb_io::intensity_network_stream::first_packet_listener fpl = std::bind(&l1_server::first_packet_received, this, std::placeholders::_1);
        
	ini_params.ipaddr = config.ipaddr[istream];
	ini_params.udp_port = config.port[istream];
        ini_params.nbeams = nbeams_per_stream;
	ini_params.nupfreq = xdiv(config.nfreq, ch_frb_io::constants::nfreq_coarse_tot);
        ini_params.nrfifreq = config.nrfifreq;
	ini_params.nt_per_packet = config.nt_per_packet;
	ini_params.fpga_counts_per_sample = config.fpga_counts_per_sample;
	ini_params.nt_align = config.nt_align;
	ini_params.stream_id = istream + 1;   // +1 here since first NFS mount is /frb-archive-1, not /frb-archive-0
        ini_params.frame0_url = config.frame0_url;
        ini_params.frame0_timeout = config.frame0_timeout;
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
        if (config.ignore_end_of_stream)
            ini_params.accept_end_of_stream_packets = false;

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
        input_streams[istream]->add_first_packet_listener(fpl);

        stream_reset_coordinators[istream] = make_shared<stream_coordinator>(nbeams_per_stream);

        /* FIXME -- drop support for stream_beam_ids ??  Do via RPC?

	// This is the subset of 'stream_beam_ids' which is processed by stream 'istream'.
	vector<int> sf_beam_ids;
	for (int b: config.stream_beam_ids)
	    if (vcontains(ini_params.beam_ids, b))
		sf_beam_ids.push_back(b);
	// If config.stream_filename_pattern is an empty string, then stream_to_files() doesn't do anything.
        bool need_rfi = (config.nrfifreq > 0);
	input_streams[istream]->stream_to_files(config.stream_filename_pattern, sf_beam_ids, 0, need_rfi);   // (pattern, priority)
         */
    }
}


void l1_server::make_rpc_servers()
{
    if (rpc_servers.size() || rpc_threads.size())
	throw("ch-frb-l1 internal error: double call to make_rpc_servers()");
    if (input_streams.size() != size_t(config.nstreams))
	throw("ch-frb-l1 internal error: make_rpc_servers() was called, without first calling make_input_streams()");
    if (bonsai_dedispersers.size() != size_t(config.nbeams))
        throw("ch-frb-l1 internal error: make_rpc_servers() was called, without first calling spawn_dedispersion_threads");
    if (injectors.size() != size_t(config.nbeams))
        throw("ch-frb-l1 internal error: make_rpc_servers() was called, without first calling spawn_dedispersion_threads");

    // Wait for all bonsai dedispersers to be created.
    chlog("RPC servers: waiting for bonsai dedispersers to be created...");
    while (true) {
        {
            ulock u(bonsai_dedisp_mutex);
            int gotn = 0;
            for (int i=0; i<config.nbeams; i++)
                if (bonsai_dedispersers_set[i])
                    gotn++;
            if (gotn == config.nbeams)
                break;
        }
        usleep(100000);
    }

    // Split into light-weight and heavy-weight RPC servers?
    bool heavy = config.heavy_rpc_address.size();
    
    this->rpc_servers.resize(config.nstreams);
    this->rpc_threads.resize(config.nstreams);
    if (heavy) {
        this->heavy_rpc_servers.resize(config.nstreams);
        this->heavy_rpc_threads.resize(config.nstreams);
    }

    int nbeams_per_stream = xdiv(config.nbeams, config.nstreams);

    for (int istream = 0; istream < config.nstreams; istream++) {
        // Grab the subset of bonsai dedispersers for this stream.
        // NOTE, at this point all dedispersers have been set, so we don't need
        // to lock the mutex any more.
        vector<shared_ptr<const bonsai::dedisperser> > rpc_bonsais;
        vector<tuple<int, string, shared_ptr<const rf_pipelines::pipeline_object> > > rpc_latency;
        vector<shared_ptr<rf_pipelines::intensity_injector> > inj(nbeams_per_stream);
        for (int ib = 0; ib < nbeams_per_stream; ib++) {
            inj[ib] = injectors[istream * nbeams_per_stream + ib];
            rpc_bonsais.push_back(bonsai_dedispersers[istream * nbeams_per_stream + ib]);
            rpc_latency.push_back(make_tuple(ib, "before_rfi",
                                             latency_monitors_pre[istream * nbeams_per_stream + ib]));
            rpc_latency.push_back(make_tuple(ib, "after_rfi",
                                             latency_monitors_post[istream * nbeams_per_stream + ib]));
        }

        if (heavy) {
            vector<shared_ptr<rf_pipelines::intensity_injector> > empty_inj;
            // Light-weight RPC server gets no injectors
            rpc_servers[istream] = make_shared<L1RpcServer> (input_streams[istream], empty_inj, mask_stats_maps[istream], rpc_bonsais, false, config.rpc_address[istream], command_line, rpc_latency);
            // Heavy-weight RPC server does get injectors
            heavy_rpc_servers[istream] = make_shared<L1RpcServer> (input_streams[istream], inj, mask_stats_maps[istream], rpc_bonsais, true, config.heavy_rpc_address[istream], command_line, rpc_latency);
            heavy_rpc_threads[istream] = heavy_rpc_servers[istream]->start();
        } else {
            // ?? Allow the single RPC server to support heavy-weight RPCs?
            rpc_servers[istream] = make_shared<L1RpcServer> (input_streams[istream], inj, mask_stats_maps[istream], rpc_bonsais, true, config.rpc_address[istream], command_line, rpc_latency);
        }
	rpc_threads[istream] = rpc_servers[istream]->start();
    }
    chlog("Created RPC servers.");
}

void l1_server::make_prometheus_servers()
{
    if (prometheus_servers.size())
	throw("ch-frb-l1 internal error: double call to make_prometheus_servers()");
    if (input_streams.size() != size_t(config.nstreams))
	throw("ch-frb-l1 internal error: make_prometheus_servers() was called, without first calling make_input_streams()");
    this->prometheus_servers.resize(config.nstreams);
    for (int istream = 0; istream < config.nstreams; istream++) {
        prometheus_servers[istream] = start_prometheus_server(config.prometheus_address[istream], input_streams[istream], mask_stats_maps[istream]);
    }
}

void l1_server::make_mask_stats()
{
    for (int istream = 0; istream < config.nstreams; istream++) {
        mask_stats_maps.push_back(make_shared<mask_stats_map>());
    }
}

void l1_server::spawn_dedispersion_threads()
{
    if (dedispersion_threads.size())
	throw("ch-frb-l1 internal error: double call to spawn_dedispersion_threads()");
    if (input_streams.size() != size_t(config.nstreams))
	throw("ch-frb-l1 internal error: spawn_dedispersion_threads() was called, without first calling make_input_streams()");
    if (stream_reset_coordinators.size() != size_t(config.nstreams))
        throw("ch-frb-l1 internal error: spawn_dedispersion_threads() was called, without first calling make_input_streams()");
    if (l1b_subprocesses.size() != size_t(config.nbeams))
	throw("ch-frb-l1 internal error: spawn_dedispersion_threads() was called, without first calling spawn_l1b_subprocesses()");

    if (config.l1_verbosity >= 1)
	cout << "ch-frb-l1: spawning " << config.nbeams << " dedispersion thread(s)" << endl;

    dedispersion_threads.resize(config.nbeams);
    injectors.resize(config.nbeams);
    bonsai_dedispersers.resize(config.nbeams);
    bonsai_dedispersers_set.resize(config.nbeams, false);
    latency_monitors_pre.resize(config.nbeams);
    latency_monitors_post.resize(config.nbeams);

    for (int ibeam = 0; ibeam < config.nbeams; ibeam++)
        this->injectors[ibeam] = rf_pipelines::make_intensity_injector(ch_frb_io::constants::nt_per_assembled_chunk);
    
    std::function<void(int, shared_ptr<const bonsai::dedisperser>,
                       shared_ptr<const rf_pipelines::pipeline_object>,
                       shared_ptr<const rf_pipelines::pipeline_object>)> set_bonsai =
        std::bind(&l1_server::set_bonsai, this, std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3, std::placeholders::_4);

    for (int ibeam = 0; ibeam < config.nbeams; ibeam++) {
	int nbeams_per_stream = xdiv(config.nbeams, config.nstreams);
	int istream = ibeam / nbeams_per_stream;

	dedispersion_thread_context context;
	context.config = this->config;
	context.sp = this->input_streams[istream];
        context.reset_coord = this->stream_reset_coordinators[istream];
        context.injector_transform = this->injectors[ibeam];
        context.ms_map = this->mask_stats_maps[istream];
        context.set_bonsai = set_bonsai;
	context.l1b_subprocess = this->l1b_subprocesses[ibeam];
	context.allowed_cores = this->dedispersion_cores[ibeam];
	context.asynchronous_dedispersion = this->asynchronous_dedispersion;
	context.ibeam = ibeam;
	context.stream_ibeam = ibeam % nbeams_per_stream;

	if (config.l1_verbosity >= 2) {
	    cout << "ch-frb-l1: spawning dedispersion thread " << ibeam //<< ", beam_id=" << config.beam_ids[ibeam] 
		 << ", stream=" << context.sp->ini_params.ipaddr << ":" << context.sp->ini_params.udp_port
		 << ", allowed_cores=" << ch_frb_io::vstr(context.allowed_cores) 
		 << ", asynchronous_dedispersion=" << context.asynchronous_dedispersion << endl;
	}

	dedispersion_threads[ibeam] = std::thread(dedispersion_thread_main, context);
    }
}

void l1_server::set_bonsai(int ibeam,
                           shared_ptr<const bonsai::dedisperser> bonsai,
                           shared_ptr<const rf_pipelines::pipeline_object> latency_pre,
                           shared_ptr<const rf_pipelines::pipeline_object> latency_post) {
    chlog("set_bonsai(" << ibeam << ")");
    ulock u(bonsai_dedisp_mutex);
    bonsai_dedispersers[ibeam] = bonsai;
    bonsai_dedispersers_set[ibeam] = true;
    latency_monitors_pre [ibeam] = latency_pre;
    latency_monitors_post[ibeam] = latency_post;
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

    for (auto rpc : rpc_servers)
        rpc->do_shutdown();
    for (auto rpc : heavy_rpc_servers)
        rpc->do_shutdown();
    for (auto& th : rpc_threads)
        th.join();
    for (auto& th : heavy_rpc_threads)
        th.join();
    for (auto& prom : prometheus_servers)
        prom.reset();
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


