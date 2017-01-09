#include <unistd.h>
#include <pthread.h>

#include <zmq.hpp>
#include <msgpack.hpp>

//#include "l1-ringbuf.hpp"
#include "ch_frb_io.hpp"

#include "l1-rpc.hpp"
#include "rpc.hpp"

using namespace std;
using namespace ch_frb_io;

/*
 The L1 RPC server is structured as a typical ZeroMQ multi-threaded
 server (eg, http://zguide.zeromq.org/cpp:asyncsrv).


 FIXME: does this load-balance correctly?  Or does the server process
 DEALER socket round-robin messages to the workers?


 There is a client-facing ZeroMQ socket, _frontend, listening on a TCP
 port.  The main server thread (run()) pulls requests off this socket.
 It answers simple requests immediately, and queues long-running
 requests for worker thread(s) to process.

 Long-running requests include the "write_chunks" request, which
 writes assembled_chunks to disk.

 write_chunks requests are represented as write_chunk_request structs
 and placed in a priority queue.  (We manage the priority-queue aspect
 ourselves, because there are some complexities.)  Each time a new
 request is enqueued, the server sends an empty message to the worker
 threads.  This signals them to pull a write_chunk_request off the
 queue.

 The main RPC server communicates with the RPC worker thread(s) using
 the in-memory _backend ZeroMQ socket.


 FIXME: why don't we just use a mutex & condition variable?  Don't
 really need to have the RPC worker read from the ZMQ socket to get a
 signal that work is available.


 FIXME: should the RPC Server reply to a write_chunks request with a
 quick reply saying how many chunks the client should expect?


 */


// For debugging, convert a zmq message to a string
static string msg_string(zmq::message_t &msg) {
    return string(static_cast<const char*>(msg.data()), msg.size());
}

// helper for the next function
static void myfree(void* p, void*) {
    ::free(p);
}
// Convert a msgpack buffer to a ZeroMQ message; the buffer is released.
static zmq::message_t* sbuffer_to_message(msgpack::sbuffer &buffer) {
    zmq::message_t* msg = new zmq::message_t(buffer.data(), buffer.size(), myfree);
    buffer.release();
    return msg;
}

/*
 A class for the RPC worker thread that writes assembled_chunks to
 disk as requested by clients.
 */
class RpcWorker {
public:
    RpcWorker(zmq::context_t* ctx, L1RpcServer* server) :
        _socket(*ctx, ZMQ_DEALER),
        _server(server) {
    }

    void run() {
        _socket.connect("inproc://rpc-backend");

        while (true) {
            zmq::message_t msg;

            // Wait for a message... this blocks until a
            // write_chunk_request is available.  The message itself
            // is empty and is ignored.
            _socket.recv(&msg);

            // Pull a write_chunk_request off the queue!
            write_chunk_request w = _server->pop_write_request();

            //cout << "Worker got write request: beam " << w.chunk->beam_id << ", chunk " << w.chunk->ichunk << ", FPGA counts " << (w.chunk->isample * w.chunk->fpga_counts_per_sample) << endl;

            WriteChunks_Reply rep;
            rep.beam = w.chunk->beam_id;
            rep.chunk = w.chunk->ichunk;
            rep.success = false;
            rep.filename = w.filename;

            try {
                w.chunk->msgpack_bitshuffle = true;
                w.chunk->write_msgpack_file(w.filename);
                rep.success = true;
            } catch (...) {
                cout << "Write msgpack file failed: filename " << rep.filename << endl;
                rep.error_message = "Failed to write msgpack file";
            }
            // Drop this chunk so its memory can be reclaimed
            w.chunk.reset();

            // Format the reply sent to the client(s).
            msgpack::sbuffer buffer;
            msgpack::pack(buffer, rep);
            zmq::message_t* reply = sbuffer_to_message(buffer);

            // Send reply to each client waiting for this chunk.
            for (size_t i = 0; i < w.clients.size(); i++) {
                zmq::message_t* client = w.clients[i];
                zmq::message_t* thisreply = reply;
                if ((w.clients.size() > 1) && (i < (w.clients.size()-1))) {
                    // make a copy for all but last client.
                    thisreply = new zmq::message_t();
                    thisreply->copy(reply);
                }
                if (!(_socket.send(*client, ZMQ_SNDMORE) &&
                      _socket.send(*thisreply))) {
                    cout << "ERROR: sending RPC reply: " << strerror(zmq_errno()) << endl;
                }
                delete client;
                delete thisreply;
            }
            delete reply;
        }
    }

private:
    zmq::socket_t _socket;
    L1RpcServer* _server;
};

struct rpc_worker_thread_context {
    zmq::context_t* ctx;
    L1RpcServer* server;
};

static void* rpc_worker_thread_main(void *opaque_arg) {
    rpc_worker_thread_context *context = reinterpret_cast<rpc_worker_thread_context *>(opaque_arg);
    zmq::context_t* ctx = context->ctx;
    L1RpcServer* server = context->server;
    delete context;

    RpcWorker rpc(ctx, server);
    rpc.run();
    return NULL;
}

L1RpcServer::L1RpcServer(zmq::context_t &ctx, string port,
                     shared_ptr<ch_frb_io::intensity_network_stream> stream) :
    _ctx(ctx),
    _frontend(_ctx, ZMQ_ROUTER),
    _backend(_ctx, ZMQ_DEALER),
    _port(port),
    _stream(stream)
{
    // Require messages sent on the frontend socket to have valid addresses.
    _frontend.setsockopt(ZMQ_ROUTER_MANDATORY, 1);

    pthread_mutex_init(&this->_q_lock, NULL);
}

L1RpcServer::~L1RpcServer() {
    pthread_mutex_destroy(&this->_q_lock);
}

write_chunk_request L1RpcServer::pop_write_request() {
    write_chunk_request wreq;
    pthread_mutex_lock(&this->_q_lock);
    if (_write_reqs.empty())
        throw runtime_error(string("pop_write_request(): queue is empty"));
    wreq = _write_reqs.front();
    _write_reqs.pop_front();
    pthread_mutex_unlock(&this->_q_lock);
    return wreq;
}

// Main thread for L1 RPC server.
void L1RpcServer::run() {
    _frontend.bind(_port);
    _backend.bind("inproc://rpc-backend");

    std::vector<pthread_t*> worker_threads;
        
    // How many worker threads should be created for writing
    // assembled_chunks to disk?
    int nworkers = 1;

    // Create and start workers.
    for (int i=0; i<nworkers; i++) {
        pthread_t* thread = new pthread_t;
        rpc_worker_thread_context *context = new rpc_worker_thread_context;
        context->ctx = &_ctx;
        context->server = this;
        pthread_create(thread, NULL, rpc_worker_thread_main, context);
        worker_threads.push_back(thread);
    }

    zmq_pollitem_t pollitems[] = {
        { _frontend, 0, ZMQ_POLLIN, 0 },
        { _backend,  0, ZMQ_POLLIN, 0 },
    };

    for (;;) {
        /*{
            pthread_mutex_lock(&this->_q_lock);
            size_t n = _write_reqs.size();
            pthread_mutex_unlock(&this->_q_lock);
            cout << "L1RpcServer: polling.  Queued chunks to write: " << n << endl;
         }*/

        // Poll, waiting for new requests from clients, or replies
        //from workers.

        // int r = zmq::poll(pollitems, 2, -1);
        int r = zmq::poll(pollitems, 2, 5000);
        if (r == -1) {
            cout << "zmq::poll error: " << strerror(errno) << endl;
            break;
        }

        zmq::message_t client;
        zmq::message_t msg;
        
        if (pollitems[0].revents & ZMQ_POLLIN) {
            // New request from a client.
            //cout << "Received message from client" << endl;
            _frontend.recv(&client);
            //cout << "Client: " << msg_string(client) << endl;
            _frontend.recv(&msg);
            _handle_request(&client, &msg);
        }
        if (pollitems[1].revents & ZMQ_POLLIN) {
            // Received a reply from a worker thread.
            //cout << "Received reply from worker" << endl;
            // Check for exactly two message parts: client and reply.
            int more;
            if (_backend.recv(&client) == 0) {
                //cout << "Received zero bytes!" << endl;
                continue;
            }
            //cout << "  client: " << msg_string(client) << endl;
            more = _backend.getsockopt<int>(ZMQ_RCVMORE);
            assert(more);
            if (_backend.recv(&msg) == 0) {
                //cout << "Received zero bytes!" << endl;
                continue;
            }
            //cout << "message: " << msg.size() << " bytes" << endl;
            more = _backend.getsockopt<int>(ZMQ_RCVMORE);
            assert(!more);

            // Don't need to unpack the message -- just pass it on to the client
            /*
             msgpack::object_handle oh =
             msgpack::unpack(reinterpret_cast<const char *>(msg.data()), msg.size());
             msgpack::object obj = oh.get();
             cout << "  message: " << obj << endl;
             */
            if (!(_frontend.send(client, ZMQ_SNDMORE) &&
                  _frontend.send(msg))) {
                cout << "ERROR: sending RPC reply: " << strerror(zmq_errno()) << endl;
            }
        }
    }

    // FIXME -- join threads?
    for (int i=0; i<nworkers; i++) {
        delete worker_threads[i];
    }
}

int L1RpcServer::_handle_request(zmq::message_t* client, zmq::message_t* request) {
    const char* req_data = reinterpret_cast<const char *>(request->data());
    std::size_t offset = 0;

    // Unpack the function name (string)
    msgpack::object_handle oh = msgpack::unpack(req_data, request->size(), offset);
    string funcname = oh.get().as<string>();

    // RPC reply
    msgpack::sbuffer buffer;

    if (funcname == "get_statistics") {
        //cout << "RPC get_statistics() called" << endl;
        // No input arguments, so don't unpack anything more.

        // Gather stats...
        vector<unordered_map<string, uint64_t> > stats = _stream->get_statistics();
        msgpack::pack(buffer, stats);

        //  Send reply back to client.
        //cout << "Sending RPC reply of size " << buffer.size() << endl;
        zmq::message_t* reply = sbuffer_to_message(buffer);
        //cout << "  client: " << msg_string(*client) << endl;

        if (!(_frontend.send(*client, ZMQ_SNDMORE) &&
              _frontend.send(*reply))) {
            cout << "ERROR: sending RPC reply: " << strerror(zmq_errno()) << endl;
            return -1;
        }
        //cout << "Sent stats reply!" << endl;
        return 0;

        /*
         The get_chunks() RPC is disabled for now.
    } else if (funcname == "get_chunks") {
        cout << "RPC get_chunks() called" << endl;

        // grab GetChunks_Request argument
        msgpack::object_handle oh =
            msgpack::unpack(req_data, request.size(), offset);
        GetChunks_Request req = oh.get().as<GetChunks_Request>();

        vector<shared_ptr<assembled_chunk> > chunks;
        _get_chunks(stream, req.beams, req.min_chunk, req.max_chunk, chunks);
        // set compression flag... save original values and revert?
        for (auto it = chunks.begin(); it != chunks.end(); it++)
            (*it)->msgpack_bitshuffle = req.compress;

        msgpack::pack(buffer, chunks);
         */

    } else if (funcname == "write_chunks") {
        //cout << "RPC write_chunks() called" << endl;

        // grab WriteChunks_Request argument
        msgpack::object_handle oh = msgpack::unpack(req_data, request->size(), offset);
        WriteChunks_Request req = oh.get().as<WriteChunks_Request>();

        /*
         cout << "WriteChunks request: FPGA range " << req.min_fpga << "--" << req.max_fpga << endl;
         cout << "beams: [ ";
         for (auto beamit = req.beams.begin(); beamit != req.beams.end(); beamit++)
         cout << (*beamit) << " ";
         cout << "]" << endl;
         */

        // Retrieve the chunks requested.
        vector<shared_ptr<assembled_chunk> > chunks;
        _get_chunks(req.beams, req.min_fpga, req.max_fpga, chunks);
        //cout << "get_chunks: got " << chunks.size() << " chunks" << endl;

        for (auto chunk = chunks.begin(); chunk != chunks.end(); chunk++) {
            write_chunk_request w;
            // Format the filename the chunk will be written to.
            char* strp = NULL;
            int r = asprintf(&strp, req.filename_pattern.c_str(), (*chunk)->beam_id, (*chunk)->ichunk);
            if (r == -1) {
                cout << "Failed to format filename: " << req.filename_pattern << endl;
                continue;
            }
            w.filename = string(strp);
            free(strp);

            // Copy client ID
            zmq::message_t* client_copy = new zmq::message_t();
            client_copy->copy(client);

            // Create and enqueue a write_chunk_request for each chunk.
            w.clients.push_back(client_copy);
            w.priority = req.priority;
            w.chunk = *chunk;
            _add_write_request(w);
        }
        return 0;
    } else {
        // Silent failure?
        cout << "Error: unknown RPC function name: " << funcname << endl;
        return -1;
    }
}

// Enqueues a new request to write a chunk.        
void L1RpcServer::_add_write_request(write_chunk_request &req) {
    pthread_mutex_lock(&this->_q_lock);

    // Highest priority goes at the front of the queue.
    // Search for the first element with priority lower than this one's.
    // AND search for a higher-priority duplicate of this element.
    deque<write_chunk_request>::iterator it;
    for (it = _write_reqs.begin(); it != _write_reqs.end(); it++) {
        if (it->chunk == req.chunk) {
            //cout << "Found an existing write request for chunk: beam " << req.chunk->beam_id << ", ichunk " << req.chunk->ichunk << " with >= priority" << endl;
            // Found a higher-priority existing entry -- add this request's clients to the existing one.
            it->clients.insert(it->clients.end(), req.clients.begin(), req.clients.end());
            pthread_mutex_unlock(&this->_q_lock);
            return;
        }
        if (it->priority < req.priority)
            // Found where we should insert this request!
            break;
    }
    bool added = true;
    _write_reqs.insert(it, req);
    // Now check for duplicate chunks with lower priority after this
    // newly added one.  Since the "insert" invalidates all existing
    // iterators, we have to start from scratch.

    // Iterate up to the chunk we just inserted.
    for (it = _write_reqs.begin(); it != _write_reqs.end(); it++)
        if (it->chunk == req.chunk)
            break;
    // Remember where the newly-inserted request is, because if we
    // find another request for the same chunk but lower priority,
    // we'll append clients.
    deque<write_chunk_request>::iterator newreq = it;

    it++;
    for (; it != _write_reqs.end(); it++) {
        if (it->chunk == req.chunk) {
            //cout << "Found existing write request for this chunk with priority " << it->priority << " vs " << req.priority << endl;
            // Before deleting the lower-priority entry, copy its clients.
            newreq->clients.insert(newreq->clients.end(), it->clients.begin(), it->clients.end());
            // Delete the lower-priority one.
            _write_reqs.erase(it);
            // There can only be one existing copy of this chunk, so
            // we're done.  Mark that we have not added more work
            // overall.
            added = false;
            break;
        }
    }

    /*
     cout << "Added write request: now " << _write_reqs.size() << " queued" << endl;
     cout << "Queue:" << endl;
     for (it = _write_reqs.begin(); it != _write_reqs.end(); it++) {
     cout << "  priority " << it->priority << ", chunk " << *(it->chunk) << ", clients [";
     for (auto it2 = it->clients.begin(); it2 != it->clients.end(); it2++)
     cout << " " << msg_string(**it2);
     cout << " ]" << endl;
     }
     */

    pthread_mutex_unlock(&this->_q_lock);
    if (added)
        // Send (empty) message to backend workers to trigger doing work.
        _backend.send(NULL, 0);
}

// Helper function to retrieve requested assembled_chunks from the ring buffer.
void L1RpcServer::_get_chunks(vector<uint64_t> &beams,
                            uint64_t min_fpga, uint64_t max_fpga,
                            vector<shared_ptr<assembled_chunk> > &chunks) {
    vector<vector<shared_ptr<assembled_chunk> > > ch;
    ch = _stream->get_ringbuf_snapshots(beams, min_fpga, max_fpga);
    // collapse vector-of-vectors to vector.
    for (auto beamit = ch.begin(); beamit != ch.end(); beamit++) {
        chunks.insert(chunks.end(), beamit->begin(), beamit->end());
    }
}

struct rpc_thread_context {
    shared_ptr<ch_frb_io::intensity_network_stream> stream;
    // eg, "tcp://*:5555";
    string port;
};

static void *rpc_thread_main(void *opaque_arg) {
    rpc_thread_context *context = reinterpret_cast<rpc_thread_context *> (opaque_arg);
    string port = context->port;
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    delete context;

    zmq::context_t ctx;
    L1RpcServer rpc(ctx, port, stream);
    rpc.run();
    return NULL;
}

pthread_t* l1_rpc_server_start(string port,
                               shared_ptr<ch_frb_io::intensity_network_stream> stream) {
    //cout << "Starting RPC server on " << port << endl;
    rpc_thread_context *context = new rpc_thread_context;
    context->stream = stream;
    context->port = port;

    pthread_t* rpc_thread = new pthread_t;
    int err = pthread_create(rpc_thread, NULL, rpc_thread_main, context);
    if (err)
        throw runtime_error(string("pthread_create() failed to create RPC thread: ") + strerror(errno));
    return rpc_thread;
}

