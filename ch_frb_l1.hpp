#ifndef _CH_FRB_L1_HPP
#define _CH_FRB_L1_HPP

#include <thread>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

#include <rf_pipelines.hpp>
#include <bonsai.hpp>
#include <ch_frb_io_internals.hpp>
#include <l1-rpc.hpp>
#include <l1-prometheus.hpp>

namespace ch_frb_l1 {
#if 0
}  // compiler pacifier
#endif


// -------------------------------------------------------------------------------------------------
//
// l1_config: reads and parses config files, does a lot of sanity checking,
// but does not allocate any "heavyweight" data structures.
struct l1_config
{
    l1_config() { }
    l1_config(int argc, const char **argv);

    // Command-line arguments
    std::string l1_config_filename;
    std::string rfi_config_filename;
    std::string bonsai_config_filename;
    std::string l1b_config_filename;

    Json::Value rfi_transform_chain_json;
    bonsai::config_params bonsai_config;

    // Command-line flags
    // Currently, l1_verbosity can have the following values (I may add more later):
    //   1: pretty quiet
    //   2: pretty noisy

    bool tflag = false;
    bool rflag = false;
    bool fflag = false;
    bool mflag = false;
    
    bool l1b_pipe_io_debug = false;
    bool memory_pool_debug = false;
    bool write_chunk_debug = false;
    bool deliberately_crash = false;
    bool ignore_end_of_stream = false;
    int l1_verbosity = 1;

    // nstreams is automatically determined by the number of (ipaddr, port) pairs.
    // There will be one (network_thread, assembler_thread, rpc_server) triple for each stream.
    int nbeams = 0;
    int nfreq = 0;
    int nstreams = 0;
    int nt_per_packet = 0;
    int fpga_counts_per_sample = 384;
    int nt_align = 0;   // used to align stream of assembled_chunks to RFI/dedispersion block size.

    // The number of frequencies in the downsampled RFI chain.
    // Must match the number of downsampled frequencies in the RFI chain JSON file.  (probably 1024)
    int nrfifreq = 0;

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
    std::vector<std::string> ipaddr;
    std::vector<int> port;

    // One L1-RPC per stream for light calls, one for heavy-weight
    std::vector<std::string> rpc_address;
    std::vector<std::string> heavy_rpc_address;

    // One L1-prometheus per stream
    std::vector<std::string> prometheus_address;
    // Optional chlog logging server address
    std::string logger_address;

    // Size of RFI mask measurement ringbuffer (15 seconds required for prometheus; more could be useful for other monitoring tools)
    int rfi_mask_meas_history;

    // Buffer sizes, as specified in config file.
    int unassembled_ringbuf_nsamples = 4096;
    int assembled_ringbuf_nsamples = 8192;
    std::vector<int> telescoping_ringbuf_nsamples;
    double write_staging_area_gb = 0.0;
    
    // "Derived" unassembled ringbuf parameters.
    int unassembled_ringbuf_capacity = 0;
    int unassembled_nbytes_per_list = 0;
    int unassembled_npackets_per_list = 0;

    // "Derived" assembled and telescoping ringbuf parameters.
    int assembled_ringbuf_nchunks = 0;
    std::vector<int> telescoping_ringbuf_nchunks;

    // Parameters of the memory_slab_pool(s)
    int nbytes_per_memory_slab = 0;     // size (in bytes) of memory slab used to store assembled_chunk
    int total_memory_slabs = 0;         // total for node

    // List of output devices (e.g. '/ssd', '/nfs')
    std::vector<std::string> output_device_names;

    // If 'intensity_prescale' is specified, then all intensity values will be multiplied by its value.
    // This is a workaround for 16-bit overflow issues in bonsai.  When data is saved to disk, the
    // prescaling will not be applied.
    float intensity_prescale = 1.0;

    // L1b linkage.  Note: assumed L1b command line is:
    //   <l1b_executable_filename> <l1b_config_filename> <beam_id>

    std::string l1b_executable_filename;
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
    
    std::string stream_devname;            // specified in config file, options are "ssd" or "nfs", defaults to "ssd"
    std::string stream_acqname;            // specified in config file, defaults to "", which results in no streaming acquisition
    std::vector<int> stream_beam_ids;      // specified in config file, defaults to all beam ID's on node
    std::string stream_filename_pattern;   // derived from 'stream_devname' and 'stream_acqname'

    void _have_warnings() const;
};


// -------------------------------------------------------------------------------------------------
//
// This master data structure defines an L1 server instance.

class stream_coordinator;

struct l1_server {        
    using corelist_t = std::vector<int>;

    const l1_config config;
    
    int ncpus = 0;
    std::vector<corelist_t> cores_on_cpu;   // length-ncpus, defines mapping of cores to cpus
    
    // Core-pinning scheme
    corelist_t output_thread_cores;             // assumed same corelist for all I/O threads
    corelist_t network_thread_cores;            // assumed same for all network threads
    std::vector<corelist_t> dedispersion_cores;      // length config.nbeams, used for (RFI, dedisp, L1B).
    std::vector<corelist_t> assembler_thread_cores;  // length nstreams
    bool asynchronous_dedispersion = false;     // run RFI and dedispersion in separate threads?
    double sleep_hack = 0.0;                    // a temporary kludge that will go away soon

    std::string command_line;

    // "Heavyweight" data structures.
    std::vector<std::shared_ptr<bonsai::trigger_pipe>> l1b_subprocesses;   // can be vector of empty pointers, if L1B is not being run.
    std::vector<std::shared_ptr<ch_frb_io::output_device>> output_devices;
    std::vector<std::shared_ptr<ch_frb_io::memory_slab_pool>> memory_slab_pools;
    std::vector<std::shared_ptr<ch_frb_io::intensity_network_stream>> input_streams;
    std::vector<std::shared_ptr<stream_coordinator> > stream_reset_coordinators;

    std::vector<std::shared_ptr<mask_stats_map> > mask_stats_maps;
    std::vector<std::shared_ptr<L1RpcServer>> rpc_servers;
    std::vector<std::shared_ptr<L1RpcServer>> heavy_rpc_servers;
    std::vector<std::shared_ptr<std::atomic<bool>>> rpc_servers_alive;
    std::vector<std::shared_ptr<std::atomic<bool>>> heavy_rpc_servers_alive;
    std::vector<std::shared_ptr<L1PrometheusServer>> prometheus_servers;
    std::vector<std::shared_ptr<rf_pipelines::intensity_injector> > injectors;
    std::vector<std::thread> rpc_threads;
    std::vector<std::thread> heavy_rpc_threads;
    std::vector<std::thread> dedispersion_threads;
    std::mutex bonsai_dedisp_mutex;
    std::vector<std::shared_ptr<const bonsai::dedisperser> > bonsai_dedispersers;
    std::vector<bool> bonsai_dedispersers_set;

    std::vector<std::shared_ptr<const rf_pipelines::pipeline_object> > latency_monitors_pre;
    std::vector<std::shared_ptr<const rf_pipelines::pipeline_object> > latency_monitors_post;

    // The constructor reads the configuration files, does a lot of sanity checks,
    // but does not initialize any "heavyweight" data structures.
    l1_server(int argc, const char **argv);

    // These methods incrementally construct the "heavyweight" data structures.
    void start_logging();
    void spawn_l1b_subprocesses();
    void make_output_devices();
    void make_memory_slab_pools();
    void make_input_streams();
    void make_mask_stats();
    void make_rpc_servers();
    void make_prometheus_servers();
    void spawn_dedispersion_threads();

    // callback registered with intensity_network_stream
    void first_packet_received(std::vector<int> beams);
    
    // These methods wait for the server to exit, and print some summary info.
    void join_all_threads();
    void print_statistics();

    // Helper methods called by constructor.
    void _init_subscale();
    void _init_20cores_16beams();
    void _init_20cores_8beams();

    // Called-back by dedispersion thread
    void set_bonsai(int ibeam, std::shared_ptr<const bonsai::dedisperser>,
                    std::shared_ptr<const rf_pipelines::pipeline_object> latency_pre,
                    std::shared_ptr<const rf_pipelines::pipeline_object> latency_post);
};



// -------------------------------------------------------------------------------------------------
//
// file_utils.cpp


extern bool file_exists(const std::string &filename);
extern bool is_directory(const std::string &filename);
extern bool is_empty_directory(const std::string &dirname);

extern std::vector<std::string> listdir(const std::string &dirname);

extern size_t disk_space_used(const std::string &dirname);

// Note: umask will be applied to 'mode'
extern void makedir(const std::string &filename, bool throw_exception_if_directory_exists=true, mode_t mode=0777);

// "devname" should be either "ssd" or "nfs".
//
// There used to be an arbitrary string 'acqdir_base' here, but I got nervous since if someone accidentally specified
// a directory in the node's root filesystem (as opposed to local SSD or "large" NFS server), it would blow up the
// poor head node with more NFS traffic than it can handle!
//
// The only purpose of the 'stream_ids' argument is to check that /frb-archiver-X is mounted, for all X in stream_ids.
//
// If "new_acq" is false, assume that we're not starting a new
// acquisition, so it's okay if the directories are not empty.  This
// is required, eg, if we're turning on or off different beams within
// an acq.
extern std::string acqname_to_filename_pattern(const std::string &devname,
					       const std::string &acqname,
					       const std::vector<int> &stream_ids,
                                               const std::vector<int> &beam_ids,
                                               bool new_acq=true);

// Converts "/local/acq_data/(ACQNAME)/beam_(BEAM)/chunk_(CHUNK).msg"
// to "/local/acq_data/(ACQNAME)"
extern std::string acq_pattern_to_dir(const std::string &pattern);


// -------------------------------------------------------------------------------------------------
//
// Inlines


inline int xdiv(int num, int den)
{
    assert((num > 0) && (den > 0) && (num % den == 0));
    return num / den;
}

inline bool is_power_of_two(int n)
{
    assert(n >= 1);
    return (n & (n-1)) == 0;
}

inline int round_up_to_power_of_two(double x)
{
    assert(x > 0.0 && x < 1.0e9);
    return 1 << int(ceil(log2(x)));
}

inline int round_down_to_power_of_two(double x)
{
    assert(x >= 1.0 && x < 1.0e9);
    return 1 << int(floor(log2(x)));
}

inline std::vector<int> vrange(int lo, int hi)
{
    assert(lo <= hi);

    std::vector<int> ret(hi-lo);
    for (int i = lo; i < hi; i++)
	ret[i-lo] = i;

    return ret;
}

inline std::vector<int> vrange(int n)
{
    return vrange(0,n);
}

template<typename T>
inline std::vector<T> vconcat(const std::vector<T> &v1, const std::vector<T> &v2)
{
    size_t n1 = v1.size();
    size_t n2 = v2.size();

    std::vector<T> ret(n1+n2);
    memcpy(&ret[0], &v1[0], n1 * sizeof(T));
    memcpy(&ret[n1], &v2[0], n2 * sizeof(T));
    return ret;
}


template<typename T>
inline bool vcontains(const std::vector<T> &v, T x)
{
    for (size_t i = 0; i < v.size(); i++) {
	if (v[i] == x)
	    return true;
    }

    return false;
}


// -------------------------------------------------------------------------------------------------
//
// yaml_paramfile


struct yaml_paramfile {
    const std::string filename;
    YAML::Node yaml;
    int verbosity;

    std::unordered_set<std::string> all_keys;
    mutable std::unordered_set<std::string> requested_keys;

    // The 'verbosity' constructor argument has the following meaning:
    //   0 = quiet
    //   1 = announce default values for all unspecified parameters
    //   2 = announce all parameters
    yaml_paramfile(const std::string &filename, int verbosity=0);

    bool has_param(const std::string &k) const;
    bool check_for_unused_params(bool fatal=true) const;

    // For debugging and message-printing
    virtual void _die(const std::string &txt) const;    // by default, throws an exception
    virtual void _print(const std::string &txt) const;  // by default, prints to cout
    
    template<typename T> static inline std::string type_name();
    template<typename T> static inline std::string stringify(const T &x);
    template<typename T> static inline std::string stringify(const std::vector<T> &x);


    // _read_scalar1(): helper for read_scalar(), assumes key exists
    template<typename T>
    T _read_scalar1(const std::string &k) const
    {
	try {
	    return yaml[k].as<T> ();
	}
	catch (...) { }

	_die(std::string("expected '") + k + std::string("' to have type ") + type_name<T>());
	throw std::runtime_error("yaml_paramfile::_die() returned?!");
    }

    // _read_scalar2(): helper for read_scalar(), assumes key exists
    template<typename T>
    T _read_scalar2(const std::string &k) const
    {
	T ret = _read_scalar1<T> (k);
	requested_keys.insert(k);

	if (verbosity >= 2)
	    _print(k + " = " + stringify(ret) + "\n");

	return ret;
    }

    // "Vanilla" version of read_scalar()
    template<typename T>
    T read_scalar(const std::string &k) const
    {
	if (!has_param(k))
	    _die("parameter '" + k + "' not found");

	return _read_scalar2<T> (k);
    }

    // This version of read_scalar() has a default value, which is returned if the key is not found.
    template<typename T>
    T read_scalar(const std::string &k, T default_val) const
    {
	if (!has_param(k)) {
	    if (verbosity >= 1)
		_print("parameter '" + k + "' not found, using default value " + stringify(default_val) + "\n");
	    return default_val;
	}

	return _read_scalar2<T> (k);
    }


    // _read_vector1(): helper for read_vector(), assumes key exists
    // Automatically converts a scalar to length-1 vector.
    template<typename T>
    std::vector<T> _read_vector1(const std::string &k) const
    {
	try {
	    return yaml[k].as<std::vector<T>> ();
	}
	catch (...) { }

	try {
	    return { yaml[k].as<T>() };
	}
	catch (...) { }

	_die("expected '" + k + "' to have type " + type_name<T>() + ", or be a list of " + type_name<T>() + "s");
	throw std::runtime_error("yaml_paramfile::_die() returned?!");	
    }

    // _read_vector2(): helper for read_vector(), assumes key exists
    template<typename T>
    std::vector<T> _read_vector2(const std::string &k) const
    {
	std::vector<T> ret = _read_vector1<T> (k);
	requested_keys.insert(k);

	if (verbosity >= 2)
	    _print(k + " = " + stringify(ret) + "\n");

	return ret;
    }

    // "Vanilla" version of read_vector().
    // Automatically converts a scalar to length-1 vector.
    template<typename T>
    std::vector<T> read_vector(const std::string &k) const
    {
	if (!has_param(k))
	    _die("parameter '" + k + "' not found");

	return _read_vector2<T> (k);
    }

    // This version of read_vector() has a default value, which is returned if the key is not found.
    template<typename T>
    std::vector<T> read_vector(const std::string &k, const std::vector<T> default_val) const
    {
	if (!has_param(k)) {
	    if (verbosity >= 1)
		_print("parameter '" + k + "' not found, using default value " + stringify(default_val) + "\n");
	    return default_val;
	}

	return _read_vector2<T> (k);
    }
};


template<> inline std::string yaml_paramfile::type_name<int> () { return "int"; }
template<> inline std::string yaml_paramfile::type_name<bool> () { return "bool"; }
template<> inline std::string yaml_paramfile::type_name<float> () { return "float"; }
template<> inline std::string yaml_paramfile::type_name<double> () { return "double"; }
template<> inline std::string yaml_paramfile::type_name<std::string> () { return "string"; }


template<typename T> inline std::string yaml_paramfile::stringify(const T &x)
{
    std::stringstream ss;
    ss << x;
    return ss.str();
}

template<typename T> inline std::string yaml_paramfile::stringify(const std::vector<T> &x)
{
    std::stringstream ss;
    ss << "[ ";
    for (size_t i = 0; i < x.size(); i++)
	ss << (i ? ", " : "") << x[i];
    ss << " ]";
    return ss.str();
}


}  // namespace ch_frb_l1

#endif  // _CH_FRB_L1_HPP
