#include <fstream>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <thread>
#include <queue>

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


// -------------------------------------------------------------------------------------------------
//
// Utils


typedef std::lock_guard<std::mutex> scoped_lock;
typedef std::unique_lock<std::mutex> ulock;


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

// Create a tiny ZeroMQ message containing the given integer, msgpacked.
static zmq::message_t* token_to_message(uint32_t token) {
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, token);
    zmq::message_t* replymsg = sbuffer_to_message(buffer);
    return replymsg;
}

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


// -------------------------------------------------------------------------------------------------
//
// l1_backend_queue
//
// This data structure is a just thread-safe queue of (WriteChunks_Reply, client, token) triples.
// It is used to send WriteChunks_Reply objects back to the RpcServer (from an I/O thread).
//
// The 'client' and 'token' are needed so that the RpcServer can send its asynchronous WriteChunks_Reply.
// If 'client' is a null pointer, this means that no asynchronous WriteChunks_Reply should be sent.


class l1_backend_queue {
public:
    struct entry {
	zmq::message_t *client = nullptr;
	uint32_t token = 0;
	WriteChunks_Reply reply;   // note: includes (filename, success, error_message)
    };

    void enqueue_write_reply(const shared_ptr<entry> &e);

    // Note: dequeue_write_reply() is nonblocking!  
    // If the queue is empty, it immediately returns an empty pointer.
    shared_ptr<entry> dequeue_write_reply();

protected:
    std::queue<shared_ptr<entry>> _entries;
    std::mutex _lock;
};


void l1_backend_queue::enqueue_write_reply(const shared_ptr<l1_backend_queue::entry> &e)
{
    if (!e)
	throw runtime_error("internal error: null pointer in l1_backend_queue::enqueue_write_reply()");

    lock_guard<mutex> lg(_lock);
    _entries.push(e);
}

shared_ptr<l1_backend_queue::entry> l1_backend_queue::dequeue_write_reply()
{
    shared_ptr<l1_backend_queue::entry> ret;
    lock_guard<mutex> lg(_lock);

    if (!_entries.empty()) {
	ret = _entries.front();
	_entries.pop();
    }

    return ret;
}


// -------------------------------------------------------------------------------------------------
//
// l1_write_request
//
// Reminder: the chunk-writing path now works as follows.  When we want to write a chunk, we
// send an object of type 'ch_frb_io::write_chunk_request' to the I/O thread pool in ch_frb_io.
// If the virtual "callback" write_chunk_request::write_callback() is defined, then the I/O thread
// will call this function when the write request completes.
//
// The l1_write_request is a subclass of ch_frb_io::write_chunk_request, whose callback function
// constructs a WriteChunks_Reply object, and puts it in the RpcServer's backend_queue.
//
// The upshot is: if the RpcServer submits l1_write_requests to the output_device_pool, then a
// WriteChunks_Reply object will show up in RpcServer::_backend_queue when the write completes.


struct l1_write_request : public ch_frb_io::write_chunk_request {
    // Note: inherits the following members from write_chunk_request base class
    //    shared_ptr<ch_frb_io::assembled_chunk> chunk;
    //    string filename;
    //    int priority;

    shared_ptr<l1_backend_queue> backend_queue;
    zmq::message_t *client = NULL;
    uint32_t token = 0;

    virtual void write_callback(const string &error_message) override;
};


void l1_write_request::write_callback(const string &error_message)
{
    shared_ptr<l1_backend_queue::entry> e = make_shared<l1_backend_queue::entry> ();
    e->client = this->client;
    e->token = this->token;

    WriteChunks_Reply &rep = e->reply;
    rep.beam = chunk->beam_id;
    rep.fpga0 = chunk->fpga_begin;
    rep.fpgaN = chunk->fpga_end - chunk->fpga_begin;
    rep.success = (error_message.size() == 0);
    rep.filename = this->filename;
    rep.error_message = error_message;

    //cout << "l1_write_request: write_callback for " << rep.filename << "success " << rep.success << ", errmsg " << rep.error_message << endl;
    
    backend_queue->enqueue_write_reply(e);
}


// -------------------------------------------------------------------------------------------------


L1RpcServer::L1RpcServer(shared_ptr<ch_frb_io::intensity_network_stream> stream,
                         const string &port,
                         zmq::context_t *ctx) :
    _ctx(ctx ? ctx : new zmq::context_t()),
    _created_ctx(ctx == NULL),
    _frontend(*_ctx, ZMQ_ROUTER),
    _backend_queue(make_shared<l1_backend_queue>()),
    _output_devices(stream->ini_params.output_devices),
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
    if (_created_ctx)
        delete _ctx;
}

bool L1RpcServer::is_shutdown() {
    scoped_lock l(_q_mutex);
    return _shutdown;
}

void L1RpcServer::set_writechunk_status(string filename,
                                        string status,
                                        string error_message) {
    {
        ulock u(_status_mutex);
        _write_chunk_status[filename] = make_pair(status, error_message);
	u.unlock();

	string s = (error_message.size() > 0) ? (", error_message='" + error_message + "'") : "";
	chlog("Set writechunk status for " << filename << " to " << status << s);
    }
}

// For testing purposes, enqueue a chunk for writing as though an RPC
// client requested it.
void L1RpcServer::enqueue_write_request(std::shared_ptr<ch_frb_io::assembled_chunk> chunk,
                                        std::string filename,
                                        int priority) {
    shared_ptr<l1_write_request> w = make_shared<l1_write_request> ();
    w->chunk = chunk;
    w->filename = filename;
    w->priority = priority;
    w->backend_queue = this->_backend_queue;

    bool ret = _output_devices.enqueue_write_request(w);

    if (!ret) {
	// Request failed to queue.  In testing/debugging, it makes most sense to throw an exception.
	throw runtime_error("L1RpcServer::enqueue_write_request: filename '" + filename + "' failed to queue");
    }
}


// Main thread for L1 RPC server.
void L1RpcServer::run() {
    chime_log_set_thread_name("L1-RPC-server");
    chlog("bind(" << _port << ")");
    _frontend.bind(_port);
    
    // Important: we set a timeout on the frontend socket, so that the RPC server
    // will check the backend_queue (and the shutdown flag) every 100 milliseconds.

    int timeout = 100;   // milliseconds
    _frontend.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

    // Main receive loop.  
    // We check the shutdown flag and the backend_queue in every iteration.
    while (!is_shutdown()) {
	_check_backend_queue();

        zmq::message_t client;
        zmq::message_t msg;

	// New request.  Should be exactly two message parts.
	int more;
	bool ok;

	ok = _frontend.recv(&client);
	if (!ok) {
	    if (errno == EAGAIN)
		continue;   // Timed out, back to top of main receive loop...
	    chlog("Failed to receive message on frontend socket!");
	    break;
	}
	chlog("Received RPC request from client: " << msg_string(client));
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
    }

    chlog("L1 RPC server: exiting.");
}

std::thread L1RpcServer::start() {
    thread t(std::bind(&L1RpcServer::run, this));
    // t.detach();  // commented out, since we now join the thread in ch-frb-l1.cpp
    return t;
}

void L1RpcServer::do_shutdown() {
    ulock u(_q_mutex);
    _stream->end_stream();
    _shutdown = true;
    u.unlock();
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

    chlog("Received RPC request for function: '" << funcname << "'");
    //from client '" << msg_string(*client) << "'");

    if (funcname == "shutdown") {
        chlog("Shutdown requested.");
        do_shutdown();
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

    } else if (funcname == "get_packet_rate_matrix") {
        // grab *start* and *period* arguments
        msgpack::object_handle oh = msgpack::unpack(req_data, request->size(), offset);
        if (oh.get().via.array.size != 2) {
            chlog("get_packet_rate_matrix RPC: failed to parse input arguments");
            return -1;
        }
        double start = oh.get().via.array.ptr[0].as<double>();
        double period = oh.get().via.array.ptr[1].as<double>();
        chlog("get_packet_rate_matrix: start " << start << ", period " << period);

        shared_ptr<packet_counts> rate = _stream->get_packet_rates(start, period);
        chlog("Retrieved packet rate matrix for " << rate->start_time() << ", period " << rate->period);

        unordered_map<string, uint64_t> counts = rate->to_string();
        
        PacketRateMatrix m;
        m.start = rate->start_time();
        m.end = m.start + rate->period;
        m.receivers.push_back("me");
        std::vector<int> p;
        for (auto it = counts.begin(); it != counts.end(); it++) {
            m.senders.push_back(it->first);
            p.push_back(it->second);
        }
        m.packets.push_back(p);
        
        // Pack return value into msgpack buffer
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, m);
        //  Send reply back to client.
        zmq::message_t* reply = sbuffer_to_message(buffer);
        return _send_frontend_message(*client, *token_to_message(token), *reply);
        
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
            int arrayi = 0;

            double dt = time_diff(_time0, get_time());
            // /proc/stat values are in "jiffies"?
            stats[0]["procstat_time"] = dt * 100;

            for (; s.good();) {
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
        return _send_frontend_message(*client, *token_to_message(token),
                                     *reply);

    } else if (funcname == "list_chunks") {
       // No input arguments, so don't unpack anything more.

        // Grab snapshot of all ringbufs...
        vector<tuple<uint64_t, uint64_t, uint64_t, uint64_t> > allchunks;

        intensity_network_stream::initializer ini = _stream->ini_params;
        for (auto beamit = ini.beam_ids.begin(); beamit != ini.beam_ids.end(); beamit++) {
            // Retrieve one beam at a time
            int beam = *beamit;
            vector<int> beams;
            beams.push_back(beam);
            vector<vector<pair<shared_ptr<assembled_chunk>, uint64_t> > > chunks = _stream->get_ringbuf_snapshots(beams);
            // iterate over beams (we only requested one)
            for (auto it1 = chunks.begin(); it1 != chunks.end(); it1++) {
                // iterate over chunks
                for (auto it2 = (*it1).begin(); it2 != (*it1).end(); it2++) {
                    allchunks.push_back(make_tuple((uint64_t)it2->first->beam_id,
                                                   it2->first->fpga_begin,
                                                   it2->first->fpga_end,
                                                   it2->second));
                }
            }
        }

	// KMS: I removed code here to retrieve messages queued for writing,
	// since we're currently trying to decide whether to keep the list_chunks
	// RPC, or phase it out!  
	//
	// If we do decide to keep the list_chunks RPC , then it would be easy to
	// add a member function to 'class ch_frb_io::output_device_pool' to return
	// the chunks queued for writing.

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, allchunks);
        zmq::message_t* reply = sbuffer_to_message(buffer);
        //  Send reply back to client.
        return _send_frontend_message(*client, *token_to_message(token),
                                     *reply);

        /*
         The get_chunks() RPC is disabled for now.
    } else if (funcname == "get_chunks") {
    //cout << "RPC get_chunks() called" << endl;

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

	for (const auto &chunk: chunks) {
	    shared_ptr<l1_write_request> w = make_shared<l1_write_request> ();

            // Format the filename the chunk will be written to.
            w->filename = chunk->format_filename(req.filename_pattern);

            // Copy client ID
	    w->client = new zmq::message_t();
	    w->client->copy(client);

	    // Fill remaining fields and enqueue the write request for the I/O threads.
	    w->backend_queue = this->_backend_queue;
	    w->priority = req.priority;
	    w->chunk = chunk;
	    w->token = token;
	    
	    // Returns false if request failed to queue.  
	    bool success = _output_devices.enqueue_write_request(w);

	    // Record the status for this filename.
	    string status = success ? "QUEUED" : "FAILED";
	    string error_message = success ? "" : "write_request failed to queue";
	    set_writechunk_status(w->filename, status, error_message);

            WriteChunks_Reply rep;
            rep.beam = chunk->beam_id;
            rep.fpga0 = chunk->fpga_begin;
            rep.fpgaN = chunk->fpga_end - chunk->fpga_begin;
            rep.filename = w->filename;
            rep.success = success;
            reply.push_back(rep);
        }

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, reply);
        zmq::message_t* replymsg = sbuffer_to_message(buffer);

        return _send_frontend_message(*client, *token_to_message(token),
                                     *replymsg);

    } else if (funcname == "get_writechunk_status") {

        // grab argument: string pathname
        msgpack::object_handle oh = msgpack::unpack(req_data, request->size(), offset);
        string pathname = oh.get().as<string>();

        // search the map of pathname -> (status, error_message)
        string status;
        string error_message;
        {
            ulock u(_status_mutex);

	    // DEBUG
	    chdebug("Request for status of " << pathname);
	    for (auto it=_write_chunk_status.begin(); it!=_write_chunk_status.end(); it++) {
	      chdebug("status[" << it->first << "] = " << it->second.first << ((it->first == pathname) ? " ***" : ""));
	    }

            auto val = _write_chunk_status.find(pathname);
            if (val == _write_chunk_status.end()) {
                status = "UNKNOWN";
            } else {
                status        = val->second.first;
                error_message = val->second.second;
            }
        }
        pair<string, string> result(status, error_message);

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, result);
        //  Send reply back to client.
        zmq::message_t* reply = sbuffer_to_message(buffer);
        return _send_frontend_message(*client, *token_to_message(token),
                                      *reply);

    } else {
        // Silent failure?
        chlog("Error: unknown RPC function name: " << funcname);
        return -1;
    }
}


// Called periodically by the RpcServer thread.
void L1RpcServer::_check_backend_queue()
{
    for (;;) {
	shared_ptr<l1_backend_queue::entry> w = _backend_queue->dequeue_write_reply();
	if (!w)
	    return;

	// We received a WriteChunks_Reply from the I/O thread pool!
	// We need to do two things: update the writechunk_status hash, and
	// forward the reply to the client (if the 'client' pointer is non-null).

	const WriteChunks_Reply &rep = w->reply;
	
	// Set writechunk_status.
	const char *status = rep.success ? "SUCCEEDED" : "FAILED";
	set_writechunk_status(rep.filename, status, rep.error_message);

	zmq::message_t *client = w->client;
        //cout << "Write request finished: " << rep.filename << " " << status << endl;
	if (!client)
	    continue;

	// Format the reply sent to the client.
	msgpack::sbuffer buffer;
	msgpack::pack(buffer, rep);
	zmq::message_t* reply = sbuffer_to_message(buffer);
	zmq::message_t* token_msg = token_to_message(w->token);

	_send_frontend_message(*client, *token_msg, *reply);

	delete reply;
	delete client;
	delete token_msg;
    }
}


int L1RpcServer::_send_frontend_message(zmq::message_t& clientmsg,
                                        zmq::message_t& tokenmsg,
                                        zmq::message_t& contentmsg) {
    try {
        if (!(_frontend.send(clientmsg, ZMQ_SNDMORE) &&
              _frontend.send(tokenmsg, ZMQ_SNDMORE) &&
              _frontend.send(contentmsg))) {
            chlog("ERROR: sending RPC reply: " << strerror(zmq_errno()));
            return -1;
        }
    } catch (const zmq::error_t& e) {
        chlog("ERROR: sending RPC reply: " << e.what());
        return -1;
    }
    return 0;
}


// Helper function to retrieve requested assembled_chunks from the ring buffer.
void L1RpcServer::_get_chunks(const vector<int> &beams,
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
