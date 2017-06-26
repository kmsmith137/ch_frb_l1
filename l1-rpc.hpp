#ifndef L1_RPC_H_
#define L1_RPC_H_

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <zmq.hpp>
#include <ch_frb_io.hpp>

const int default_port_l1_rpc = 5555;

// implementation detail: a struct used to communicate between threads
// of the RPC server.
struct write_chunk_request;

// The main L1 RPC server object.
class L1RpcServer {
public:
    // Creates a new RPC server listening on the given port, and reading
    // from the ring buffers of the given stream.
    L1RpcServer(std::shared_ptr<ch_frb_io::intensity_network_stream> stream,
                const std::string &port = "",
                zmq::context_t* ctx = NULL);
                
    ~L1RpcServer();

    // Main RPC service loop.  Does not return.
    void run();

    // Start the RPC service thread.
    std::thread start();

    // Returns True if an RPC shutdown() call has been received.
    bool is_shutdown();

    // called by RPC worker threads.
    write_chunk_request* pop_write_request();

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

    // enqueues the given request to write an assembled_chunk to disk;
    // will be processed by worker threads.  Handles the priority
    // queuing.
    void _add_write_request(write_chunk_request* req);

    // retrieves assembled_chunks overlapping the given range of
    // FPGA-count values from the ring buffers for the given beam IDs.
    void _get_chunks(std::vector<uint64_t> &beams,
                     uint64_t min_fpga, uint64_t max_fpga,
                     std::vector<std::shared_ptr<ch_frb_io::assembled_chunk> > &chunks);

    void _do_shutdown();

    int _send_frontend_message(zmq::message_t& clientmsg,
                               zmq::message_t& tokenmsg,
                               zmq::message_t& contentmsg);

private:
    // ZeroMQ context
    zmq::context_t* _ctx;

    // true if we created _ctx and thus should delete it
    bool _created_ctx;

    // Client-facing socket
    zmq::socket_t _frontend;

    // RPC worker thread-facing socket.
    zmq::socket_t _backend;

    // Port the client-facing socket is listening on.
    std::string _port;

    // the queue of write requests to be run by the RpcWorker(s)
    std::deque<write_chunk_request*> _write_reqs;
    // (and the mutex for it)
    std::mutex _q_mutex;
    // (and a conditions variable for when the queue is not empty)
    std::condition_variable _q_cond;

    // a vector of result codes for write_chunk_request() calls.
    std::unordered_map<std::string, std::pair<std::string, std::string> > _write_chunk_status;
    // (and the mutex for it)
    std::mutex _status_mutex;

    // flag when we are shutting down.
    bool _shutdown;

    // the stream we are serving RPC requests for.
    std::shared_ptr<ch_frb_io::intensity_network_stream> _stream;

    // server start time
    struct timeval _time0;
};


#endif
