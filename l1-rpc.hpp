#ifndef L1_RPC_H_
#define L1_RPC_H_

#include <vector>
#include <deque>
#include <thread>
#include <map>
#include <mutex>
#include <condition_variable>
#include <zmq.hpp>
#include <ch_frb_io.hpp>
#include <rf_pipelines.hpp>
#include <rpc.hpp>
#include <mask_stats.hpp>

const int default_port_l1_rpc = 5555;

// implementation detail: a struct used to communicate between I/O threads and the RpcServer.
class l1_backend_queue;

class chunk_status_map {
public:
    void set(const std::string& filename, const std::string& status, const std::string& error_message);
    bool get(const std::string& filename, std::string& status, std::string& error_message);
protected:
    // result codes for write_chunk_request() calls:
    //  filename -> pair(status, error_message)
    std::map<std::string, std::pair<std::string, std::string> > _write_chunk_status;
    // (and the mutex for it)
    std::mutex _status_mutex;
};


// The main L1 RPC server object.
class L1RpcServer {
public:
    // Creates a new RPC server listening on the given port, and reading
    // from the ring buffers of the given stream.
    L1RpcServer(std::shared_ptr<ch_frb_io::intensity_network_stream> stream,
                std::vector<std::shared_ptr<rf_pipelines::intensity_injector> > injectors,
                std::shared_ptr<const ch_frb_l1::mask_stats_map> maskstats,
                std::vector<std::shared_ptr<const bonsai::dedisperser> > bonsais =
                std::vector<std::shared_ptr<const bonsai::dedisperser> >(),
                bool heavy = true,
                const std::string &port = "",
                const std::string &cmdline = "",
                std::vector<std::tuple<int, std::string, std::shared_ptr<const rf_pipelines::pipeline_object> > > monitors =
                std::vector<std::tuple<int, std::string, std::shared_ptr<const rf_pipelines::pipeline_object> > >(),
                zmq::context_t* ctx = NULL
);
                
    ~L1RpcServer();

    // Main RPC service loop.  Does not return.
    void run();

    // Start the RPC service thread.
    std::thread start();

    // Returns True if an RPC shutdown() call has been received.
    bool is_shutdown();
    
    // equivalent to receiving a shutdown() RPC.
    void do_shutdown();

    // For testing: enqueue the given chunk for writing.
    void enqueue_write_request(std::shared_ptr<ch_frb_io::assembled_chunk>,
                               std::string filename,
                               int priority = 0);

protected:
    // responds to the given RPC request, either sending immediate
    // reply or queuing work for worker threads.
    int _handle_request(zmq::message_t* client, zmq::message_t* request);

    int _handle_streaming_request(zmq::message_t* client, std::string funcname, uint32_t token,
                                  const char* req_data, std::size_t length, std::size_t& offset);
                                  
    int _handle_stream_status(zmq::message_t* client, std::string funcname, uint32_t token,
                              const char* req_data, std::size_t length, std::size_t& offset);

    int _handle_packet_rate(zmq::message_t* client, std::string funcname, uint32_t token,
                            const char* req_data, std::size_t length, std::size_t& offset);

    int _handle_packet_rate_history(zmq::message_t* client, std::string funcname, uint32_t token,
                                    const char* req_data, std::size_t length, std::size_t& offset);

    int _handle_get_statistics(zmq::message_t* client, std::string funcname, uint32_t token,
                               const char* req_data, std::size_t length, std::size_t& offset);

    int _handle_list_chunks(zmq::message_t* client, std::string funcname, uint32_t token,
                            const char* req_data, std::size_t length, std::size_t& offset);

    int _handle_write_chunks(zmq::message_t* client, std::string funcname, uint32_t token,
                             const char* req_data, std::size_t length, std::size_t& offset);

    int _handle_masked_freqs(zmq::message_t* client, std::string funcname, uint32_t token,
                             const char* req_data, std::size_t length, std::size_t& offset);

    int _handle_masked_freqs_2(zmq::message_t* client, std::string funcname, uint32_t token,
                             const char* req_data, std::size_t length, std::size_t& offset);

    int _handle_max_fpga(zmq::message_t* client, std::string funcname, uint32_t token,
                             const char* req_data, std::size_t length, std::size_t& offset);
    
    std::string _handle_inject(const char* req_data, size_t req_size, size_t req_offset);

    std::string _handle_fork(bool start, const char* req_data, size_t req_size, size_t req_offset);

    void _check_backend_queue();

    // retrieves assembled_chunks overlapping the given range of
    // FPGA-count values from the ring buffers for the given beam IDs.
    void _get_chunks(const std::vector<int> &beams,
                     uint64_t min_fpga, uint64_t max_fpga,
                     std::vector<std::shared_ptr<ch_frb_io::assembled_chunk> > &chunks);

    int _send_frontend_message(zmq::message_t& clientmsg,
                               zmq::message_t& tokenmsg,
                               zmq::message_t& contentmsg);

    void _update_n_chunks_waiting(bool inc);

    std::shared_ptr<const bonsai::dedisperser> _get_bonsai_for_beam(int beam);
    std::shared_ptr<rf_pipelines::intensity_injector> _get_injector_for_beam(int beam);
    
private:
    // The command line that launched this L1 process
    std::string _command_line;

    // Are we doing heavy-weight RPCs?
    bool _heavy;
    
    // ZeroMQ context
    zmq::context_t* _ctx;

    // true if we created _ctx and thus should delete it
    bool _created_ctx;

    // Client-facing socket
    zmq::socket_t _frontend;

    // The "backend queue" is used by the I/O threads, to send WriteChunk_Reply
    // objects back to the RpcServer, when write requests complete.
    std::shared_ptr<l1_backend_queue> _backend_queue;
    
    // Pool of I/O threads (one thread for each physical device), which accept 
    // write_chunk_requests from the RpcServer.
    ch_frb_io::output_device_pool _output_devices;

    // Port the client-facing socket is listening on.
    std::string _port;

    std::shared_ptr<chunk_status_map> _chunk_status;

    // How many chunks are we currently waiting for?
    int _n_chunks_writing;

    // Only protects _shutdown!
    std::mutex _q_mutex;

    // flag when we are shutting down.
    bool _shutdown;

    // the stream we are serving RPC requests for.
    std::shared_ptr<ch_frb_io::intensity_network_stream> _stream;

    // the injector_transforms for the beams we are running.
    std::vector<std::shared_ptr<rf_pipelines::intensity_injector> > _injectors;

    // objects holding RFI mask statistics
    std::shared_ptr<const ch_frb_l1::mask_stats_map> _mask_stats;

    // Bonsai dedisperser objects (used for latency reporting)
    std::vector<std::shared_ptr<const bonsai::dedisperser> > _bonsais;

    std::map<int, std::shared_ptr<const bonsai::dedisperser> > _beam_to_bonsai;
    std::map<int, std::shared_ptr<rf_pipelines::intensity_injector> > _beam_to_injector;

    // Latency monitors
    std::vector<std::tuple<int, std::string, std::shared_ptr<const rf_pipelines::pipeline_object> > > _latencies;
    
    // server start time
    struct timeval _time0;
};


#endif
