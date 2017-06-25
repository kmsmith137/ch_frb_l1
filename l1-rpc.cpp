#include <fstream>
#include <unistd.h>
#include <sys/time.h>
#include <thread>

#include <zmq.hpp>
#include <msgpack.hpp>

#include "ch_frb_io.hpp"

#include "l1-rpc.hpp"
#include "rpc.hpp"
#include "chlog.hpp"

using namespace std;
using namespace ch_frb_io;

/*
 The L1 RPC server is structured as a typical ZeroMQ multi-threaded
 server (eg, http://zguide.zeromq.org/cpp:asyncsrv), with some tweaks
 as described below.

 There is a client-facing ZeroMQ socket, _frontend, listening on a TCP
 port.  The main server thread (run()) pulls requests off this socket.
 It answers simple requests immediately, and queues long-running
 requests for worker thread(s) to process.

 Long-running requests include the "write_chunks" request, which
 writes assembled_chunks to disk.

 write_chunks requests are represented as write_chunk_request structs
 and placed in a priority queue.  (We manage the priority-queue aspect
 ourselves, because there are some complexities.)

 The main RPC server communicates with the RPC worker thread(s) using
 the in-memory _backend ZeroMQ socket.

 In the typical ZeroMQ multi-threaded server, the main server process
 communicates with the worker processes by sending messages on the
 _backend socket, but instead we use our work queue and a condition
 variable.  This provides for load-balancing the worker threads.

 The RPC calls are fully asynchronous; a single request may generate
 zero or more replies.  To keep things organized, each request has a
 header with the function name, and an integer "token".  Each reply to
 that request includes that token.


 Additional notes:
 
 - I originally wanted the client to use a single ROUTER socket to
 talk to multiple servers (each with ROUTER sockets), but it turns out
 that ROUTER-to-ROUTER is tricky to get working correctly... in fact,
 I got a python implementation working but never did get the C++
 server (which I thought was the same code) working; the issue is that
 ROUTER sockets don't want to send a message to a peer until they have
 exchanged a handshake with the peer; this happens *asynchronously*
 after a connect() call, and there does not appear to be a way to find
 out when that process is finished.  So instead the rpc_client.py uses
 one DEALER socket to talk to each server, and uses poll() to listen
 for messages from any of them.  This polling is not a busy-loop, and
 it uses only one or two ZeroMQ threads per context.

 */


// For debugging, convert a zmq message to a string
/*
static string msg_string(zmq::message_t &msg) {
    return string(static_cast<const char*>(msg.data()), msg.size());
}
 */

// RPC requests to write assembled_chunks to disk are stored in this struct
struct write_chunk_request {
    std::vector<std::pair<zmq::message_t*, uint32_t> > clients;
    std::string filename;
    int priority;
    std::shared_ptr<ch_frb_io::assembled_chunk> chunk;
};

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

// Create a tiny ZeroMQ message containing the given integer, msgpacked.
static zmq::message_t* token_to_message(uint32_t token) {
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, token);
    zmq::message_t* replymsg = sbuffer_to_message(buffer);
    return replymsg;
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

    void run(int iworker) {
        chime_log_set_thread_name("RPC-worker-" + std::to_string(iworker));
        _socket.connect("inproc://rpc-backend");

        while (true) {
            zmq::message_t msg;

            // Pull a write_chunk_request off the queue!
            write_chunk_request* w = _server->pop_write_request();
            if (!w) {
                // Quit!
                chlog("Rpc worker: received NULL write_chunk_request; exiting.");
                break;
            }

            //cout << "Worker got write request: beam " << w->chunk->beam_id << ", chunk " << w->chunk->ichunk << ", FPGA counts " << (w->chunk->isample * w->chunk->fpga_counts_per_sample) << endl;

            WriteChunks_Reply rep;
            rep.beam = w->chunk->beam_id;
            rep.fpga0 = w->chunk->fpgacounts_begin();
            rep.fpgaN = w->chunk->fpgacounts_N();
            rep.success = false;
            rep.filename = w->filename;

            try {
                w->chunk->msgpack_bitshuffle = true;
                w->chunk->write_msgpack_file(w->filename);
                rep.success = true;
            } catch (...) {
                chlog("Write msgpack file failed: filename " << rep.filename);
                rep.error_message = "Failed to write msgpack file";
            }
            // Drop this chunk so its memory can be reclaimed
            w->chunk.reset();

            // Testing :)
            //sleep(3);

            // Format the reply sent to the client(s).
            msgpack::sbuffer buffer;
            msgpack::pack(buffer, rep);
            zmq::message_t* reply = sbuffer_to_message(buffer);

            // Send reply to each client waiting for this chunk.
            for (size_t i = 0; i < w->clients.size(); i++) {
                zmq::message_t* client = w->clients[i].first;
                uint32_t token = w->clients[i].second;
                zmq::message_t* thisreply = reply;
                if ((w->clients.size() > 1) && (i < (w->clients.size()-1))) {
                    // make a copy for all but last client.
                    thisreply = new zmq::message_t();
                    thisreply->copy(reply);
                }
                try {
                    if (!(_socket.send(*client, ZMQ_SNDMORE) &&
                          _socket.send(*token_to_message(token), ZMQ_SNDMORE) &&
                          _socket.send(*thisreply))) {
                        chlog("ERROR: sending RPC reply: " << strerror(zmq_errno()));
                    }
                } catch (const zmq::error_t& e) {
                    chlog("ERROR sending RPC reply: " << e.what());
                }
                delete client;
                delete thisreply;
            }
            // this request is done.
            delete w;
        }
    }

private:
    zmq::socket_t _socket;
    L1RpcServer* _server;
};

inline struct timeval get_time()
{
    struct timeval ret;
    if (gettimeofday(&ret, NULL) < 0)
	throw std::runtime_error("gettimeofday() failed");
    return ret;
}
inline double time_diff(const struct timeval &tv1, const struct timeval &tv2)
{
    return (tv2.tv_sec - tv1.tv_sec) + 1.0e-6 * (tv2.tv_usec - tv1.tv_usec);
}

L1RpcServer::L1RpcServer(shared_ptr<ch_frb_io::intensity_network_stream> stream,
                         const string &port,
                         zmq::context_t *ctx) :
    _ctx(ctx ? ctx : new zmq::context_t()),
    _created_ctx(ctx == NULL),
    _frontend(*_ctx, ZMQ_ROUTER),
    _backend (*_ctx, ZMQ_DEALER),
    _shutdown(false),
    _stream(stream)
{
    if (port.length())
        _port = port;
    else
        _port = "tcp://*:" + std::to_string(default_port_l1_rpc);

    _time0 = get_time();

    // Set my identity
    _frontend.setsockopt(ZMQ_IDENTITY, _port);
    // Require messages sent on the frontend socket to have valid addresses.
    _frontend.setsockopt(ZMQ_ROUTER_MANDATORY, 1);
}


L1RpcServer::~L1RpcServer() {
    _frontend.close();
    _backend.close();
    if (_created_ctx)
        delete _ctx;
}

typedef std::lock_guard<std::mutex> scoped_lock;
typedef std::unique_lock<std::mutex> ulock;

bool L1RpcServer::is_shutdown() {
    scoped_lock l(_q_mutex);
    return _shutdown;
}

write_chunk_request* L1RpcServer::pop_write_request() {
    write_chunk_request* wreq;

    ulock u(_q_mutex);
    for (;;) {
        if (_shutdown) {
            wreq = NULL;
            break;
        }
        if (_write_reqs.empty()) {
            _q_cond.wait(u);
            continue;
        }
        wreq = _write_reqs.front();
        _write_reqs.pop_front();
        break;
    }
    u.unlock();
    return wreq;
}

void L1RpcServer::enqueue_write_request(std::shared_ptr<ch_frb_io::assembled_chunk> chunk,
                                        std::string filename,
                                        int priority) {
    write_chunk_request* w = new write_chunk_request();
    w->filename = filename;
    w->priority = priority;
    w->chunk = chunk;
    _add_write_request(w);
}


// Main thread for L1 RPC server.
void L1RpcServer::run() {
    chime_log_set_thread_name("L1-RPC-server");
    chlog("bind(" << _port << ")");
    _frontend.bind(_port);
    _backend.bind("inproc://rpc-backend");

    std::vector<std::thread> worker_threads;
    std::vector<RpcWorker*> workers;

    // How many worker threads should be created for writing
    // assembled_chunks to disk?
    int nworkers = 2;

    // Create and start workers.
    for (int i=0; i<nworkers; i++) {
        RpcWorker* w = new RpcWorker(_ctx, this);
        workers.push_back(w);
        worker_threads.push_back(std::thread(std::bind(&RpcWorker::run, w, i)));
    }

    // convert _frontend and _backend to void* (explicitly) calling socket_t.operator void*()
    void* p_front = _frontend.operator void*();
    void* p_back  = _backend .operator void*();

    zmq_pollitem_t pollitems[] = {
        { p_front, 0, ZMQ_POLLIN, 0 },
        { p_back,  0, ZMQ_POLLIN, 0 },
    };

    for (;;) {
        /*{
         // lock
         size_t n = _write_reqs.size();
         cout << "L1RpcServer: polling.  Queued chunks to write: " << n << endl;
         }*/

        // Poll, waiting for new requests from clients, or replies
        //from workers.

        // int r = zmq::poll(pollitems, 2, -1);
        try {
            int r = zmq::poll(pollitems, 2, 5000);
            if (r == -1) {
                chlog("zmq::poll error: " << strerror(errno));
                break;
            }
        } catch (const zmq::error_t& e) {
            chlog("error in poll: " << e.what());
            // This can happen when the main thread exits, L1RpcServer
            // destructor gets called, zmq context destroyed...
            //_do_shutdown();
            ulock u(_q_mutex);
            _shutdown = true;
            u.unlock();
            _q_cond.notify_all();
            break;
        }

        zmq::message_t client;
        zmq::message_t msg;

        if (pollitems[0].revents & ZMQ_POLLIN) {
            // New request.  Should be exactly two message parts.
            int more;
            bool ok;
            //cout << "Receiving message on frontend socket" << endl;
            ok = _frontend.recv(&client);
            if (!ok) {
                chlog("Failed to receive message on frontend socket!");
                continue;
            }
            more = _frontend.getsockopt<int>(ZMQ_RCVMORE);
            if (!more) {
                chlog("Expected two message parts on frontend socket!");
                continue;
            }
            ok = _frontend.recv(&msg);
            if (!ok) {
                chlog("Failed to receive second message on frontend socket!");
                continue;
            }
            more = _frontend.getsockopt<int>(ZMQ_RCVMORE);
            if (more) {
                chlog("Expected only two message parts on frontend socket!");
                // recv until !more?
                while (more) {
                    ok = _frontend.recv(&msg);
                    if (!ok) {
                        chlog("Failed to recv() while dumping bad message on frontend socket!");
                        break;
                    }
                    more = _frontend.getsockopt<int>(ZMQ_RCVMORE);
                }
                continue;
            }

            try {
                _handle_request(&client, &msg);
            } catch (const std::exception& e) {
                chlog("Warning: Failed to handle RPC request... ignoring!  Error: " << e.what());
                try {
                    msgpack::object_handle oh = msgpack::unpack(reinterpret_cast<const char *>(msg.data()), msg.size());
                    msgpack::object obj = oh.get();
                    chlog("  message: " << obj);
                } catch (...) {
                    chlog("  failed to un-msgpack message");
                }
            }
            if (_shutdown)
                break;
        }
        if (pollitems[1].revents & ZMQ_POLLIN) {
            // Received a reply from a worker thread.
            //cout << "Received reply from worker" << endl;

            // Assert exactly three message parts: client, token, and
            // reply.  This is strictly within-process communication,
            // so wrong message formats mean an error in the code.
            int more;
            bool ok;
            zmq::message_t token;
            ok = _backend.recv(&client);
            assert(ok);
            more = _backend.getsockopt<int>(ZMQ_RCVMORE);
            assert(more);
            ok = _backend.recv(&token);
            assert(ok);
            more = _backend.getsockopt<int>(ZMQ_RCVMORE);
            assert(more);
            ok = _backend.recv(&msg);
            assert(ok);
            more = _backend.getsockopt<int>(ZMQ_RCVMORE);
            assert(!more);
            //cout << "  client: " << msg_string(client) << endl;
            //cout << "message: " << msg.size() << " bytes" << endl;

            // Don't need to unpack the message -- just pass it on to the client
            /*
             msgpack::object_handle oh =
             msgpack::unpack(reinterpret_cast<const char *>(msg.data()), msg.size());
             msgpack::object obj = oh.get();
             cout << "  message: " << obj << endl;
             */
            if (!(_frontend.send(client, ZMQ_SNDMORE) &&
                  _frontend.send(token, ZMQ_SNDMORE) &&
                  _frontend.send(msg))) {
                chlog("ERROR: sending RPC reply: " << strerror(zmq_errno()));
            }
        }
    }




    chlog("L1 RPC server: broke out of main loop.  Joining workers...");

    // join worker threads
    for (int i=0; i<nworkers; i++) {
        worker_threads[i].join();
        delete workers[i];
    }

    chlog("L1 RPC server: exiting.");
}

std::thread L1RpcServer::start() {
    thread t(std::bind(&L1RpcServer::run, this));
    t.detach();
    return t;
}

void L1RpcServer::_do_shutdown() {
    ulock u(_q_mutex);
    _stream->end_stream();
    _shutdown = true;
    u.unlock();
    _q_cond.notify_all();
    _stream->join_threads();
}

int L1RpcServer::_handle_request(zmq::message_t* client, zmq::message_t* request) {
    const char* req_data = reinterpret_cast<const char *>(request->data());
    std::size_t offset = 0;

    // Unpack the function name (string)
    msgpack::object_handle oh = msgpack::unpack(req_data, request->size(), offset);
    Rpc_Request rpcreq = oh.get().as<Rpc_Request>();
    string funcname = rpcreq.function;
    uint32_t token = rpcreq.token;

    if (funcname == "shutdown") {
        chlog("Shutdown requested.");
        _do_shutdown();
        return 0;

    } else if ((funcname == "start_logging") ||
               (funcname == "stop_logging")) {
        // grab address argument
        msgpack::object_handle oh = msgpack::unpack(req_data, request->size(), offset);
        string addr = oh.get().as<string>();
        chlog("Logging request: " << funcname << ", address " << addr);

        if (funcname == "start_logging") {
            chime_log_add_server(addr);
        } else {
            chime_log_remove_server(addr);
        }
        return 0;

    } else if (funcname == "get_statistics") {
        //cout << "RPC get_statistics() called" << endl;
        // No input arguments, so don't unpack anything more.

        // Gather stats...
        vector<unordered_map<string, uint64_t> > stats = _stream->get_statistics();

        {
            // This is a bit of a HACK!
            // read and parse /proc/stat, add to stats[0].
            ifstream s("/proc/stat");
            string key;
            int arrayi;

            double dt = time_diff(_time0, get_time());
            // /proc/stat values are in "jiffies"?
            stats[0]["procstat_time"] = dt * 100;

            for (;;) {
                string word;
                s >> word;
                if ((word.size() == 0) && (s.eof()))
                    break;
                if (key.size() == 0) {
                    key = word;
                    arrayi = 0;
                } else {
                    uint64_t ival = stoll(word);
                    // HACK -- skip 0 values.
                    if (ival) {
                        string fullkey = "procstat_" + key + "_" + to_string(arrayi);
                        stats[0][fullkey] = ival;
                    }
                    arrayi++;
                    //cout << "set: " << fullkey << " = " << ival << endl;
                }
                if (s.peek() == '\n') {
                    key = "";
                }
            }
        }

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, stats);
        //  Send reply back to client.
        //cout << "Sending RPC reply of size " << buffer.size() << endl;
        zmq::message_t* reply = sbuffer_to_message(buffer);
        //cout << "  client: " << msg_string(*client) << endl;

        if (!(_frontend.send(*client, ZMQ_SNDMORE) &&
              _frontend.send(*token_to_message(token), ZMQ_SNDMORE) &&
              _frontend.send(*reply))) {
            chlog("ERROR: sending RPC reply: " << strerror(zmq_errno()));
            return -1;
        }
        //cout << "Sent stats reply!" << endl;
        return 0;

    } else if (funcname == "list_chunks") {
       // No input arguments, so don't unpack anything more.

        // Grab snapshot of all ringbufs...
        vector<tuple<uint64_t, uint64_t, uint64_t, uint64_t> > allchunks;

        intensity_network_stream::initializer ini = _stream->get_initializer();
        for (auto beamit = ini.beam_ids.begin(); beamit != ini.beam_ids.end(); beamit++) {
            // Retrieve one beam at a time
            int beam = *beamit;
            vector<uint64_t> beams;
            beams.push_back(beam);
            vector<vector<pair<shared_ptr<assembled_chunk>, uint64_t> > > chunks = _stream->get_ringbuf_snapshots(beams);
            // iterate over beams (we only requested one)
            for (auto it1 = chunks.begin(); it1 != chunks.end(); it1++) {
                // iterate over chunks
                for (auto it2 = (*it1).begin(); it2 != (*it1).end(); it2++) {
                    allchunks.push_back(make_tuple((uint64_t)it2->first->beam_id,
                                                   it2->first->fpgacounts_begin(),
                                                   it2->first->fpgacounts_end(),
                                                   it2->second));
                }
            }
        }
        // Add any messages queued for writing by the RPC worker threads.
        {
            ulock u(_q_mutex);
            for (auto it=_write_reqs.begin(); it!=_write_reqs.end(); it++) {
                shared_ptr<assembled_chunk> ch = (*it)->chunk;
                allchunks.push_back(make_tuple((uint64_t)ch->beam_id,
                                               ch->fpgacounts_begin(),
                                               ch->fpgacounts_end(),
                                               L1RB_WRITEQUEUE));
            }
        }

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, allchunks);
        zmq::message_t* reply = sbuffer_to_message(buffer);
        //  Send reply back to client.
        if (!(_frontend.send(*client, ZMQ_SNDMORE) &&
              _frontend.send(*token_to_message(token), ZMQ_SNDMORE) &&
              _frontend.send(*reply))) {
            chlog("ERROR: sending RPC reply: " << strerror(zmq_errno()));
            return -1;
        }
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

        // Keep a list of the chunks to be written; we'll reply right away with this list.
        vector<WriteChunks_Reply> reply;

        for (auto chunk = chunks.begin(); chunk != chunks.end(); chunk++) {
            write_chunk_request* w = new write_chunk_request();

            // Format the filename the chunk will be written to.
            w->filename = (*chunk)->format_filename(req.filename_pattern);

            // Copy client ID
            zmq::message_t* client_copy = new zmq::message_t();
            client_copy->copy(client);

            // Create and enqueue a write_chunk_request for each chunk.
            w->clients.push_back(std::make_pair(client_copy, token));
            w->priority = req.priority;
            w->chunk = *chunk;
            _add_write_request(w);

            WriteChunks_Reply rep;
            rep.beam = (*chunk)->beam_id;
            rep.fpga0 = (*chunk)->fpgacounts_begin();
            rep.fpgaN = (*chunk)->fpgacounts_N();
            rep.filename = w->filename;
            rep.success = true; // ?
            reply.push_back(rep);
        }

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, reply);
        zmq::message_t* replymsg = sbuffer_to_message(buffer);

        if (!(_frontend.send(*client, ZMQ_SNDMORE) &&
              _frontend.send(*token_to_message(token), ZMQ_SNDMORE) &&
              _frontend.send(*replymsg))) {
            chlog("ERROR: sending RPC reply: " << strerror(zmq_errno()));
            return -1;
        }

        return 0;
    } else {
        // Silent failure?
        chlog("Error: unknown RPC function name: " << funcname);
        return -1;
    }
}

// Enqueues a new request to write a chunk.
void L1RpcServer::_add_write_request(write_chunk_request* req) {

    ulock u(_q_mutex);

     // Highest priority goes at the front of the queue.
    // Search for the first element with priority lower than this one's.
    // AND search for a higher-priority duplicate of this element.
    deque<write_chunk_request*>::iterator it;
    for (it = _write_reqs.begin(); it != _write_reqs.end(); it++) {
        if ((*it)->chunk == req->chunk) {
            //cout << "Found an existing write request for chunk: beam " << req->chunk->beam_id << ", ichunk " << req->chunk->ichunk << " with >= priority" << endl;
            // Found a higher-priority existing entry -- add this request's clients to the existing one.
            (*it)->clients.insert((*it)->clients.end(), req->clients.begin(), req->clients.end());
 
            u.unlock();
            return;
        }
        if ((*it)->priority < req->priority)
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
        if ((*it)->chunk == req->chunk)
            break;
    // Remember where the newly-inserted request is, because if we
    // find another request for the same chunk but lower priority,
    // we'll append clients.
    deque<write_chunk_request*>::iterator newreq = it;

    it++;
    for (; it != _write_reqs.end(); it++) {
        if ((*it)->chunk == req->chunk) {
            //cout << "Found existing write request for this chunk with priority " << it->priority << " vs " << req->priority << endl;
            // Before deleting the lower-priority entry, copy its clients.
            (*newreq)->clients.insert((*newreq)->clients.end(), (*it)->clients.begin(), (*it)->clients.end());
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

    u.unlock();

    if (added)
        // queue not empty!
        _q_cond.notify_one();
}

// Helper function to retrieve requested assembled_chunks from the ring buffer.
void L1RpcServer::_get_chunks(vector<uint64_t> &beams,
                            uint64_t min_fpga, uint64_t max_fpga,
                            vector<shared_ptr<assembled_chunk> > &chunks) {
    vector<vector<pair<shared_ptr<assembled_chunk>, uint64_t> > > ch;
    ch = _stream->get_ringbuf_snapshots(beams, min_fpga, max_fpga);
    // collapse vector-of-vectors to vector.
    for (auto beamit = ch.begin(); beamit != ch.end(); beamit++) {
        for (auto it2 = beamit->begin(); it2 != beamit->end(); it2++) {
            chunks.push_back(it2->first);
        }
    }
}

