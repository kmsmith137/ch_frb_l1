#ifndef L1_RPC_H_
#define L1_RPC_H_

#include <vector>
#include <deque>
#include <pthread.h>
#include <zmq.hpp>
#include <ch_frb_io.hpp>

const int default_port_l1_rpc = 5555;

// High-level API: start the RPC server.
// If *port* is not specified, will listen on all IP addresses on the default port *default_port_l1_rpc*.
pthread_t* l1_rpc_server_start(std::shared_ptr<ch_frb_io::intensity_network_stream> stream,
                               std::string port = "");


// implementation detail: a struct used to communicate between threads
// of the RPC server.
struct write_chunk_request {
    std::vector<zmq::message_t*> clients;
    std::string filename;
    int priority;
    std::shared_ptr<ch_frb_io::assembled_chunk> chunk;
};

// The main L1 RPC server object.
class L1RpcServer {
public:
    // Creates a new RPC server listening on the given port, and reading
    // from the ring buffers of the given stream.
    L1RpcServer(zmq::context_t &ctx, std::string port,
                std::shared_ptr<ch_frb_io::intensity_network_stream> stream);
    ~L1RpcServer();

    // Main RPC service loop.  Does not return.
    void run();

    // called by RPC worker threads.
    write_chunk_request pop_write_request();

protected:
    // responds to the given RPC request, either sending immediate
    // reply or queuing work for worker threads.
    int _handle_request(zmq::message_t* client, zmq::message_t* request);

    // enqueues the given request to write an assembled_chunk to disk;
    // will be processed by worker threads.  Handles the priority
    // queuing.
    void _add_write_request(write_chunk_request &req);

    // retrieves assembled_chunks overlapping the given range of
    // FPGA-count values from the ring buffers for the given beam IDs.
    void _get_chunks(std::vector<uint64_t> &beams,
                     uint64_t min_fpga, uint64_t max_fpga,
                     std::vector<std::shared_ptr<ch_frb_io::assembled_chunk> > &chunks);

private:
    zmq::context_t &_ctx;

    // Client-facing socket
    zmq::socket_t _frontend;

    // RPC worker thread-facing socket.
    zmq::socket_t _backend;

    // Port the client-facing socket is listening on.
    std::string _port;

    // the queue of write requests to be run by the RpcWorker(s)
    std::deque<write_chunk_request> _write_reqs;
    // (and the mutex for it)
    pthread_mutex_t _q_lock;

    // the stream we are serving RPC requests for.
    std::shared_ptr<ch_frb_io::intensity_network_stream> _stream;
};


#endif
