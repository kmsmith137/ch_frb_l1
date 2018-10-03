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
                std::shared_ptr<const ch_frb_l1::mask_stats_map> maskstats,
                const std::string &port = "",
                const std::string &cmdline = "",
                zmq::context_t* ctx = NULL);
                
    ~L1RpcServer();

    // Main RPC service loop.  Does not return.
    void run();

    // Start the RPC service thread.
    std::thread start();

    // Returns True if an RPC shutdown() call has been received.
    bool is_shutdown();
    
    // equivalent to receiving a shutdown() RPC.
    void do_shutdown();

    // called by RPC worker threads to update status for a write_chunks request
    void set_writechunk_status(std::string filename,
                               std::string status,
                               std::string error_message);

    // For testing: enqueue the given chunk for writing.
    void enqueue_write_request(std::shared_ptr<ch_frb_io::assembled_chunk>,
                               std::string filename,
                               int priority = 0);

protected:
    // responds to the given RPC request, either sending immediate
    // reply or queuing work for worker threads.
    int _handle_request(zmq::message_t* client, zmq::message_t* request);

    void _check_backend_queue();

    // retrieves assembled_chunks overlapping the given range of
    // FPGA-count values from the ring buffers for the given beam IDs.
    void _get_chunks(const std::vector<int> &beams,
                     uint64_t min_fpga, uint64_t max_fpga,
                     std::vector<std::shared_ptr<ch_frb_io::assembled_chunk> > &chunks);

    int _send_frontend_message(zmq::message_t& clientmsg,
                               zmq::message_t& tokenmsg,
                               zmq::message_t& contentmsg);

private:
    // The command line that launched this L1 process
    std::string _command_line;

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

    // Only protects _shutdown!
    std::mutex _q_mutex;

    // flag when we are shutting down.
    bool _shutdown;

    // the stream we are serving RPC requests for.
    std::shared_ptr<ch_frb_io::intensity_network_stream> _stream;

    // objects holding RFI mask statistics
    std::shared_ptr<const ch_frb_l1::mask_stats_map> _mask_stats;

    // server start time
    struct timeval _time0;
};


#endif
