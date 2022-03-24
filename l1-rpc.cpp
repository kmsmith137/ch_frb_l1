#include <fstream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <thread>
#include <queue>
#include <cstdio>

#include <zmq.hpp>
#include <msgpack.hpp>

#include <rf_pipelines.hpp>

#if ZMQ_VERSION < ZMQ_MAKE_VERSION(4, 3, 0)
#warning "A rare race condition exists in libzmq versions before 4.3.0.  Please upgrade libzmq!"
#endif

#include "ch_frb_io.hpp"
#include "ch_frb_l1.hpp"
#include "l1-rpc.hpp"
#include "zmq-monitor.hpp"
#include "rpc.hpp"
#include "chlog.hpp"
#include "bonsai.hpp"

using namespace std;
using namespace ch_frb_io;
using namespace rf_pipelines;

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
// If the virtual write_chunk_request::status_changed() is defined, then the I/O thread
// will call this function when the write request status changes (eg, when it completes).
//
// The l1_write_request is a subclass of ch_frb_io::write_chunk_request, whose status_changed function
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
    shared_ptr<chunk_status_map> chunk_status;
    zmq::message_t *client = NULL;
    uint32_t token = 0;

    virtual void status_changed(bool finished, bool success,
                                const string &state,
                                const string &error_message) override;
};

void l1_write_request::status_changed(bool finished, bool success,
                                      const std::string &state,
                                      const string &error_message)
{
    // update status
    if (chunk_status)
        chunk_status->set(this->filename, state, error_message);
    if (!finished)
        return;

    shared_ptr<l1_backend_queue::entry> e = make_shared<l1_backend_queue::entry> (); 
    e->client = this->client;
    e->token = this->token;

    WriteChunks_Reply &rep = e->reply;
    rep.beam = chunk->beam_id;
    rep.fpga0 = chunk->fpga_begin;
    rep.fpgaN = chunk->fpga_end - chunk->fpga_begin;
    rep.success = success;
    rep.filename = this->filename;
    rep.error_message = error_message;

    //cout << "l1_write_request: write_callback for " << rep.filename << "success " << rep.success << ", errmsg " << rep.error_message << endl;
    
    backend_queue->enqueue_write_reply(e);
}


// -------------------------------------------------------------------------------------------------

void chunk_status_map::set(const string& filename,
                           const string& status,
                           const string& error_message) {
    ulock u(_status_mutex);
    _write_chunk_status[filename] = make_pair(status, error_message);
    u.unlock();

    string s = (error_message.size() > 0) ? (", error_message='" + error_message + "'") : "";
    chlog("Set writechunk status for " << filename << " to " << status << s);
}

bool chunk_status_map::get(const string& filename,
                           string& status, string& error_message) {
    ulock u(_status_mutex);
    // DEBUG
    chdebug("Request for status of " << filename);
    for (auto it=_write_chunk_status.begin(); it!=_write_chunk_status.end(); it++) {
        chdebug("status[" << it->first << "] = " << it->second.first << ((it->first == filename) ? " ***" : ""));
    }

    auto val = _write_chunk_status.find(filename);
    if (val == _write_chunk_status.end()) {
        status = "UNKNOWN";
        error_message = "";
        return false;
    } else {
        status        = val->second.first;
        error_message = val->second.second;
        return true;
    }
}

void inject_data_request::swap(rf_pipelines::intensity_injector::inject_args& dest) {
    std::swap(this->mode, dest.mode);
    std::swap(this->sample_offset, dest.sample_offset);
    std::swap(this->ndata, dest.ndata);
    std::swap(this->data, dest.data);
}

// -------------------------------------------------------------------------------------------------

L1RpcServer::L1RpcServer(shared_ptr<ch_frb_io::intensity_network_stream> stream,
                         vector<shared_ptr<rf_pipelines::intensity_injector> > injectors,
                         shared_ptr<const ch_frb_l1::mask_stats_map> ms,
                         shared_ptr<ch_frb_l1::slow_pulsar_writer_hash> sp,
                         shared_ptr<std::atomic<bool> > is_alive,
                         vector<shared_ptr<const bonsai::dedisperser> > bonsais,
                         bool heavy,
                         const string &port,
                         const string &cmdline,
                         std::vector<std::tuple<int, std::string, std::shared_ptr<const rf_pipelines::pipeline_object> > > monitors,
                         const string &name,
                         zmq::context_t *ctx
                         ) :
    _name(name),
    _command_line(cmdline),
    _heavy(heavy),
    _is_alive(is_alive),
    _ctx(ctx ? ctx : new zmq::context_t()),
    _created_ctx(ctx == NULL),
    _frontend(*_ctx, ZMQ_ROUTER),
    _backend_queue(make_shared<l1_backend_queue>()),
    _output_devices(stream->ini_params.output_devices),
    _chunk_status(make_shared<chunk_status_map>()),
    _n_chunks_writing(0),
    _shutdown(false),
    _stream(stream),
    _injectors(injectors),
    _mask_stats(ms),
    _slow_pulsar_writer_hash(sp),
    _bonsais(bonsais),
    _latencies(monitors)
{
    if ((_injectors.size() != 0) &&
        (int(_injectors.size()) != _stream->ini_params.nbeams))
        throw runtime_error("L1RpcServer: expected injectors array size to be zero or the same size as the beams array");

    if (port.length())
        _port = port;
    else
        _port = "tcp://*:" + std::to_string(default_port_l1_rpc);

    _time0 = get_time();

    // Require messages sent on the frontend socket to have valid addresses.
    _frontend.set(zmq::sockopt::router_mandatory, 1);

    // monitor zmq events on the frontend socket.
    //monitor_zmq_socket(_ctx, _frontend, _port);
}

L1RpcServer::~L1RpcServer() {
    _frontend.close();
    if (_created_ctx)
        delete _ctx;
}

void L1RpcServer::reset_beams() {
    // FIXME -- re-setup mappings _beam_to_bonsai and _beam_to_injector!!
    chlog("reset_beams()");
}

bool L1RpcServer::is_shutdown() {
    scoped_lock l(_q_mutex);
    return _shutdown;
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
    w->chunk_status = this->_chunk_status;

    bool ret = _output_devices.enqueue_write_request(w);

    if (!ret) {
	// Request failed to queue.  In testing/debugging, it makes most sense to throw an exception.
	throw runtime_error("L1RpcServer::enqueue_write_request: filename '" + filename + "' failed to queue");
    }
    _update_n_chunks_waiting(true);
}

// Main thread for L1 RPC server.
void L1RpcServer::run() {
    chime_log_set_thread_name(_name.size() ? _name : "L1-RPC-server");
    chlog("bind(" << _port << ")");
    _frontend.bind(_port);

    // HACK!! cf4n2 stream request bug
    sleep(10);

    // Important: we set a timeout on the frontend socket, so that the RPC server
    // will check the backend_queue (and the shutdown flag) every 100 milliseconds.

    int timeout = 100;   // milliseconds
    _frontend.set(zmq::sockopt::rcvtimeo, timeout);

    // Main receive loop.
    // We check the shutdown flag and the backend_queue in every iteration.
    while (!is_shutdown()) {
	_check_backend_queue();

        // Set watchdog!
        _is_alive->store(true);

        zmq::message_t client;
        zmq::message_t msg;

        // New request.  Should be exactly two message parts.
	int more;
        zmq::recv_result_t res;

	res = _frontend.recv(client);
        if (!res.has_value())
	    // EAGAIN
            continue;   // Timed out, back to top of main receive loop...
        if (!res.value()) {
	    chlog("Failed to receive message on frontend socket!");
	    break;
	}
	//chlog("Received RPC request");
	more = _frontend.get(zmq::sockopt::rcvmore);
	if (!more) {
	    chlog("Expected two message parts on frontend socket!");
	    continue;
	}
	res = _frontend.recv(msg);
	if (!res.has_value() || !res.value()) {
	    chlog("Failed to receive second message on frontend socket!");
	    continue;
	}
	more = _frontend.get(zmq::sockopt::rcvmore);
	if (more) {
	    chlog("Expected only two message parts on frontend socket!");
	    // recv until !more?
	    while (more) {
		res = _frontend.recv(msg);
		if (!res.has_value() || !res.value()) {
		    chlog("Failed to recv() while dumping bad message on frontend socket!");
		    break;
		}
		more = _frontend.get(zmq::sockopt::rcvmore);
	    }
	    continue;
	}

	try {
	    _handle_request(client, msg);
            // (note, 'client' may be empty after this returns!)
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

// Replaces all instances of the string "from" to the string "to" in
// input string "input".
static string replaceAll(const string &input, const string &from, const string &to) {
    string s = input;
    size_t i;
    while ((i = s.find(from)) != std::string::npos)
        s.replace(i, from.length(), to);
    return s;
}

struct fpga_counts_seen {
    string where;
    int beam;
    uint64_t max_fpga_seen;
    MSGPACK_DEFINE(where, beam, max_fpga_seen);
};

static string get_message_sender(zmq::message_t &msg) {
    int srcfd = msg.get(ZMQ_SRCFD);
    if (srcfd == -1)
        return "[failed to retrieve ZMQ_SRCFD]";
    struct sockaddr saddr;
    socklen_t socklen;
    int rtn = getpeername(srcfd, &saddr, &socklen);
    if (rtn == -1)
        return "[getpeername failed: " + string(strerror(errno)) + "]";
    if (saddr.sa_family != AF_INET)
        return "[getpeername sa_family = " + std::to_string(saddr.sa_family) + " != AF_INET]";
    struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(&saddr);
    return inet_ntoa(sin->sin_addr) + string(":") + std::to_string(htons(sin->sin_port));
}

int L1RpcServer::_handle_request(zmq::message_t& client, const zmq::message_t& request) {
    const char* req_data = reinterpret_cast<const char *>(request.data());
    size_t req_size = request.size();
    std::size_t offset = 0;

    // Unpack the function name (string)
    msgpack::object_handle oh = msgpack::unpack(req_data, req_size, offset);
    Rpc_Request rpcreq = oh.get().as<Rpc_Request>();
    string funcname = rpcreq.function;
    uint32_t token = rpcreq.token;

    string client_ip = get_message_sender(client);

    struct timeval tv1 = get_time();
    chlog("Received RPC request for function: '" << funcname << "', token " << token
          << " from client " << client_ip);

    int rtnval = -1;

    if (funcname == "shutdown") {
        chlog("Shutdown requested.");
        do_shutdown();
        rtnval = 0;

    } else if ((funcname == "start_logging") || (funcname == "stop_logging")) {
        // grab address argument
        msgpack::object_handle oh = msgpack::unpack(req_data, req_size, offset);
        string addr = oh.get().as<string>();
        chlog("Logging request: " << funcname << ", address " << addr);
        if (funcname == "start_logging")
            chime_log_add_server(addr);
        else
            chime_log_remove_server(addr);
        rtnval = 0;

    } else if (funcname == "stream") {

        rtnval = _handle_streaming_request(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "stream_status") {

        rtnval = _handle_stream_status(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "get_packet_rate") {

        rtnval = _handle_packet_rate(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "get_packet_rate_history") {

        rtnval = _handle_packet_rate_history(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "get_statistics") {

        rtnval = _handle_get_statistics(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "list_chunks") {

        rtnval = _handle_list_chunks(client, funcname, token, req_data, req_size, offset);

        /*
         The get_chunks() RPC is disabled for now.
    } else if (funcname == "get_chunks") {
    //cout << "RPC get_chunks() called" << endl;

        // grab GetChunks_Request argument
        msgpack::object_handle oh =
            msgpack::unpack(req_data, req_size, offset);
        GetChunks_Request req = oh.get().as<GetChunks_Request>();

        vector<shared_ptr<assembled_chunk> > chunks;
        _get_chunks(stream, req.beams, req.min_chunk, req.max_chunk, chunks);
        // set compression flag... save original values and revert?
        for (auto it = chunks.begin(); it != chunks.end(); it++)
            (*it)->msgpack_bitshuffle = req.compress;

        msgpack::pack(buffer, chunks);
         */

    } else if (funcname == "write_chunks") {
        rtnval = _handle_write_chunks(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "get_writechunk_status") {

        // grab argument: string pathname
        msgpack::object_handle oh = msgpack::unpack(req_data, req_size, offset);
        string pathname = oh.get().as<string>();

        // search the map of pathname -> (status, error_message)
        string status;
        string error_message;
        _chunk_status->get(pathname, status, error_message);
        pair<string, string> result(status, error_message);

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, result);
        //  Send reply back to client.
        zmq::message_t* reply = sbuffer_to_message(buffer);
        rtnval = _send_frontend_message(client, token, *reply);

    } else if ((funcname == "start_fork") || (funcname == "stop_fork")) {
        bool start = (funcname == "start_fork");
        string errstring = _handle_fork(start, req_data, req_size, offset);
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, errstring);
        //  Send reply back to client.
        zmq::message_t* reply = sbuffer_to_message(buffer);
        return _send_frontend_message(client, token, *reply);
        
    } else if (funcname == "inject_data") {

        string errstring = _handle_inject(req_data, req_size, offset);
        if (errstring.size())
            chlog("inject_data: return error message " << errstring);
        else
            chlog("inject_data: accepted injection (token " << token <<")");
        
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, errstring);
        //  Send reply back to client.
        zmq::message_t* reply = sbuffer_to_message(buffer);
        return _send_frontend_message(client, token, *reply);

    } else if (funcname == "get_bonsai_variances") {

        // Grab arg: list of beam numbers
        msgpack::object_handle oh = msgpack::unpack(req_data, req_size, offset);
        vector<int> beams = oh.get().as<vector<int> >();
        chlog("Requested beams: " << beams.size());

        vector<tuple<int, vector<float>, vector<float> > > result;

        if (beams.size() == 0)
            // grab all beams!
            beams = _stream->get_beam_ids();
	chlog("Grabbing variances for " << beams.size() << " beams");
        for (int b : beams) {
            auto bonsai = _get_bonsai_for_beam(b);
            if (!bonsai) {
                chlog("No bonsai object for beam " << b);
                continue;
            }
            vector<float> weights;
            vector<float> variances;
            bonsai->get_weights_and_variances(&weights, &variances);
            result.push_back(tuple<int,vector<float>,vector<float> >(b, weights, variances));
        }
	chlog("Sending variances for " << result.size() << " beams");
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, result);
        //  Send reply back to client.
        zmq::message_t* reply = sbuffer_to_message(buffer);
        return _send_frontend_message(client, token, *reply);

    } else if (funcname == "set_spulsar_writer_params"){
        msgpack::object_handle oh = msgpack::unpack(req_data, request.size(), offset);
        
        // TODO: reject requests based on file version (useful for dev)
        auto target_beam = oh.get().via.array.ptr[0].as<int>();

        auto nfreq = oh.get().via.array.ptr[1].as<int>();
        auto ntime = oh.get().via.array.ptr[2].as<int>();
        auto nbins = oh.get().via.array.ptr[3].as<int>();
        std::shared_ptr<std::string> base_path(new std::string(oh.get().via.array.ptr[4].as<std::string>()));

        const size_t nbeams = this->_stream->ini_params.nbeams;
        int target_ibeam = -1;

        shared_ptr<vector<int>> beam_ids(new vector<int>(this->_stream->get_beam_ids()));
        
        // check that the stream beam_ids 
        if (beam_ids->size() == nbeams) {
            for (size_t ibeam=0; ibeam<nbeams; ibeam++) {
                if ((*beam_ids)[ibeam] == target_beam) {
                    target_ibeam = ibeam;
                    break;
                }
            }
        }

	string result = "failure: target beam_id not found!";

        if (target_ibeam != -1) {
	    this->_slow_pulsar_writer_hash->get(target_ibeam)
		->set_params(target_beam, nfreq, ntime, nbins, base_path);
			     
	    chlog("Pulsar writer paramter update" << std::endl << "\tnfreq_out: " << nfreq
		  << std::endl <<  "\tntime_out: " << ntime << std::endl << "\tnbins: " 
		  << nbins << std::endl << "base_path: " << *base_path << std::endl);

	    result = "parameters successfully applied to slow pulsar writer";
        }

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, result);
        zmq::message_t* reply = sbuffer_to_message(buffer);
        return _send_frontend_message(client, token, *reply);
    } else if (funcname == "get_masked_frequencies") {

        rtnval = _handle_masked_freqs(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "get_masked_frequencies_2") {

        rtnval = _handle_masked_freqs_2(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "get_max_fpga_counts") {

        rtnval = _handle_max_fpga(client, funcname, token, req_data, req_size, offset);

    } else if (funcname == "get_assembler_misses") {

        vector<tuple<string, uint64_t, double> > result = _stream->get_assembler_miss_senders();
	chlog("Sending assembler_miss senders: " << result.size());
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, result);
        //  Send reply back to client.
        zmq::message_t* reply = sbuffer_to_message(buffer);
        return _send_frontend_message(client, token, *reply);
        
    } else {
        // Silent failure?
        chlog("Error: unknown RPC function name: " << funcname);
        rtnval = -1;
    }

    struct timeval tv2 = get_time();
    double dt = time_diff(tv1, tv2);
    
    chlog("Completed RPC request for function: '" << funcname << "', token " << token
          << ": " << (int)(dt*1000.) << " ms");
    return rtnval;
}

std::shared_ptr<const bonsai::dedisperser> L1RpcServer::_get_bonsai_for_beam(int beam) {
    for (size_t i=0; i<_stream->get_beam_ids().size(); i++) {
        int b = _stream->get_beam_ids()[i];
        if (b == beam) {
            if (i < _bonsais.size()) {
                return _bonsais[i];
            }
        }
    }
    return shared_ptr<bonsai::dedisperser>();
    /*
     auto it = _beam_to_bonsai.find(beam);
     if (it == _beam_to_bonsai.end()) {
     return shared_ptr<bonsai::dedisperser>();
     }
     return it->second;
     */
}

std::shared_ptr<rf_pipelines::intensity_injector> L1RpcServer::_get_injector_for_beam(int beam) {
    chlog("Searching for injector for beam " << beam << ": have " <<
          _injectors.size() << " injectors.");
    for (size_t i=0; i<_stream->get_beam_ids().size(); i++) {
        int b = _stream->get_beam_ids()[i];
        if (b == beam) {
            if (i < _injectors.size()) {
                return _injectors[i];
            }
        }
    }
    return shared_ptr<rf_pipelines::intensity_injector>();
    /*
     auto it = _beam_to_injector.find(beam);
     if (it == _beam_to_injector.end())
     return shared_ptr<rf_pipelines::intensity_injector>();
     return it->second;
     */
}

int L1RpcServer::_handle_streaming_request(zmq::message_t& client, string funcname, uint32_t token,
                                           const char* req_data, size_t req_len, size_t& offset) {

    // grab arguments: [ "acq_name", "acq_dev", "acq_meta.json",
    //                   [ beam ids ], "acq_new", (optional "acq_max_chunks") ]
    msgpack::object_handle oh = msgpack::unpack(req_data, req_len, offset);
    if (!((oh.get().via.array.size == 5) || (oh.get().via.array.size == 6))) {
        chlog("stream RPC: failed to parse input arguments: expected array of size 5 or 6");
        return -1;
    }
    string acq_name = oh.get().via.array.ptr[0].as<string>();
    string acq_dev = oh.get().via.array.ptr[1].as<string>();
    string acq_meta = oh.get().via.array.ptr[2].as<string>();
    bool acq_new = oh.get().via.array.ptr[4].as<bool>();
    int acq_max_chunks = 0;
    if (oh.get().via.array.size == 6)
        acq_max_chunks = oh.get().via.array.ptr[5].as<int>();

    // grab beam_ids
    vector<int> beam_ids;
    int nbeams = oh.get().via.array.ptr[3].via.array.size;
    for (int i=0; i<nbeams; i++) {
        int beam = oh.get().via.array.ptr[3].via.array.ptr[i].as<int>();
        // Be forgiving about requests to stream beams that we
        // aren't handling -- otherwise the client has to keep
        // track of which L1 server has which beams, which is a
        // pain.
        bool found = false;
        for (int b : _stream->get_beam_ids()) {
            if (b == beam) {
                found = true;
                break;
            }
        }
        if (found)
            beam_ids.push_back(beam);
        else
            chlog("Stream request for beam " << beam << " which isn't being handled by this node.  Ignoring.");
    }

    chlog("Stream request: \"" << acq_name << "\", device=\"" << acq_dev << "\", # requested beams: " << nbeams << ", matched " << beam_ids.size() << " beams");
    if (beam_ids.size())
        for (size_t i=0; i<beam_ids.size(); i++)
            chlog("  beam " << beam_ids[i]);
    // Default to a 1-hour stream = 3600 seconds = 3600 chunks
    if (!acq_max_chunks)
        acq_max_chunks = 3600;
    chlog("Max chunks to stream: " << acq_max_chunks);
    // Default to all beams if no beams were specified.
    if (nbeams == 0)
        beam_ids = _stream->get_beam_ids();
    // Default to acq_dev = "ssd"
    if (acq_dev.size() == 0)
        acq_dev = "ssd";
    pair<bool, string> result;
    string pattern;
    if (acq_name.size() == 0) {
        // Turn off streaming
        pattern = "";
        chlog("Turning off streaming");
        _stream->stream_to_files(pattern, { }, 0, false, 0);
        result.first = true;
        result.second = pattern;
    } else if (beam_ids.size() == 0) {
        // Request to stream a specific set of beams, none of which we have.
        chlog("No matching beams found for streaming request.");
        result.first = false;
        result.second = "No matching beams found.";
    } else {
        try {
            pattern = ch_frb_l1::acqname_to_filename_pattern(acq_dev, acq_name, { _stream->ini_params.stream_id }, _stream->get_beam_ids(), acq_new);
            chlog("Streaming to filename pattern: " << pattern);
            if (acq_new && acq_meta.size()) {
                // write metadata file in acquisition dir.
                string acqdir = ch_frb_l1::acq_pattern_to_dir(pattern);
                acqdir = replaceAll(acqdir, "(STREAM)", stringprintf("%01i", _stream->ini_params.stream_id));
                chlog("Substituted (STREAM): writing metadata to dir " << acqdir);
                string fn = acqdir + "/metadata.json";
                chlog("Writing acquisition metadata to " << fn);
                FILE* fout = std::fopen(fn.c_str(), "w");
                if (!fout) {
                    chlog("Failed to open metadata file for writing: " << fn << ": " << strerror(errno));
                } else {
                    if (std::fwrite(acq_meta.c_str(), 1, acq_meta.size(), fout) != acq_meta.size()) {
                        chlog("Failed to write " << acq_meta.size() << " bytes of metadata to " << fn << ": " << strerror(errno));
                    }
                    if (std::fclose(fout)) {
                        chlog("Failed to close metadata file: " << fn << ": " << strerror(errno));
                    }
                }
            }
            bool need_rfi = (_stream->ini_params.nrfifreq > 0);
            _stream->stream_to_files(pattern, beam_ids, 0, need_rfi, acq_max_chunks);
            result.first = true;
            result.second = pattern;
        } catch (const std::exception& e) {
            chlog("Exception!");
            result.first = false;
            result.second = e.what();
            chlog("Failed to set streaming: " << e.what());
        }
    }
    // Pack return value into msgpack buffer
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, result);
    //  Send reply back to client.
    zmq::message_t* reply = sbuffer_to_message(buffer);
    return _send_frontend_message(client, token, *reply);
}

int L1RpcServer::_handle_stream_status(zmq::message_t& client, string funcname, uint32_t token,
                                       const char* req_data, size_t req_len, size_t& offset) {
        
    unordered_map<string, string> dict;

    string stream_filename;
    vector<int> stream_beams;
    int stream_priority;
    int chunks_written;
    size_t bytes_written;
    _stream->get_streaming_status(stream_filename, stream_beams, stream_priority,
                                  chunks_written, bytes_written);

    dict["stream_filename"] = stream_filename;
    dict["stream_priority"] = std::to_string(stream_priority);
    dict["stream_chunks_written"] = std::to_string(chunks_written);
    dict["stream_bytes_written"] = std::to_string(bytes_written);
        
    // df for "ssd" and "nfs"
    struct statvfs vfs;
    if (statvfs("/frb-archiver-1", &vfs) == 0) {
        size_t gb = vfs.f_bsize * vfs.f_bavail / (1024 * 1024 * 1024);
        dict["nfs_gb_free"] = std::to_string((int)gb);
    }
    if (statvfs("/local", &vfs) == 0) {
        size_t gb = ((size_t)vfs.f_frsize * (size_t)vfs.f_bavail) / (size_t)(1024 * 1024 * 1024);
        dict["ssd_gb_free"] = std::to_string((int)gb);
    }

    // du in _last_stream_to_files
    /*
     if (_last_stream_to_files.size()) {
     // drop two directory components
     string acqdir = ch_frb_l1::acq_pattern_to_dir(_last_stream_to_files);
     try {
     size_t gb = ch_frb_l1::disk_space_used(acqdir) / (1024 * 1024 * 1024);
     dict["stream_gb_used"] = std::to_string((int)gb);
     } catch (const std::exception& e) {
     chlog("disk_space_used " << _last_stream_to_files << ": " << e.what());
     }
     }
     */

    // all my beams, as a comma-separated string
    string allbeams = "";
    for (int b : _stream->get_beam_ids()) {
        if (allbeams.size()) allbeams += ",";
        allbeams += std::to_string(b);
    }
    dict["all_beams"] = allbeams;
    // beams being streamed
    string strbeams = "";
    for (size_t i=0; i<stream_beams.size(); i++) {
        if (i) strbeams += ",";
        strbeams += std::to_string(stream_beams[i]);
    }
    dict["stream_beams"] = strbeams;
        
    // Pack return value into msgpack buffer
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, dict);
    //  Send reply back to client.
    zmq::message_t* reply = sbuffer_to_message(buffer);
    return _send_frontend_message(client, token, *reply);
}

int L1RpcServer::_handle_packet_rate(zmq::message_t& client, string funcname, uint32_t token,
                                     const char* req_data, size_t req_len, size_t& offset) {
    // grab *start* and *period* arguments
    msgpack::object_handle oh = msgpack::unpack(req_data, req_len, offset);
    if (oh.get().via.array.size != 2) {
        chlog("get_packet_rate RPC: failed to parse input arguments");
        return -1;
    }
    double start = oh.get().via.array.ptr[0].as<double>();
    double period = oh.get().via.array.ptr[1].as<double>();
    //chlog("get_packet_rate: start " << start << ", period " << period);
    shared_ptr<packet_counts> rate = _stream->get_packet_rates(start, period);
    PacketRate pr;
    if (rate) {
        pr.start = rate->start_time();
        pr.period = rate->period;
        pr.packets = rate->to_string();
        int total = 0;
        for (auto it=pr.packets.begin(); it!=pr.packets.end(); it++)
            total += it->second;
        chlog(_port << ": Retrieved packet rate for " << rate->start_time() << ", period " << rate->period << ", average counts " << (float)total/float(pr.packets.size()));
    } else {
        chlog("No packet rate available");
        pr.start = 0;
        pr.period = 0;
    }
        
    // Pack return value into msgpack buffer
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, pr);
    //  Send reply back to client.
    zmq::message_t* reply = sbuffer_to_message(buffer);
    return _send_frontend_message(client, token, *reply);
}

int L1RpcServer::_handle_packet_rate_history(zmq::message_t& client, string funcname, uint32_t token,
                                             const char* req_data, size_t req_len, size_t& offset) {
        
    // grab arguments: *start*, *end*, *period*, *l0*
    msgpack::object_handle oh = msgpack::unpack(req_data, req_len, offset);
    if (oh.get().via.array.size != 4) {
        chlog("get_packet_rate_history RPC: failed to parse input arguments");
        return -1;
    }
    double start = oh.get().via.array.ptr[0].as<double>();
    double end   = oh.get().via.array.ptr[1].as<double>();
    double period = oh.get().via.array.ptr[2].as<double>();
    vector<string> l0 = oh.get().via.array.ptr[3].as<vector<string> >();

    vector<shared_ptr<packet_counts> > packets = _stream->get_packet_rate_history(start, end, period);

    // For now, only compute the sum over L0 nodes.
    vector<double> times;
    vector<vector<double> > rates;

    for (size_t i=0; i<l0.size(); i++)
        rates.push_back(vector<double>());

    //for (int i=0; i<l0.size(); i++)
    //    chlog("packet history: getting l0 node " << l0[i]);
    //chlog("packet history: " << packets.size() << " time slices");

    // for each time slice
    for (auto it=packets.begin(); it!=packets.end(); it++) {
        int j;
        times.push_back((*it)->start_time());
        auto l0name=l0.begin();
        for (j=0; l0name != l0.end(); l0name++, j++) {
            int64_t val = 0;
            if (*l0name == "sum") {
                // for each l0->npackets pair
                for (auto it2=(*it)->counts.begin();
                     it2!=(*it)->counts.end(); it2++)
                    val += it2->second;
            } else {
                // Yuck -- convert to string:int map!
                // Should instead parse name to int and look up directly
                auto scounts = (*it)->to_string();
                auto pval = scounts.find(*l0name);
                if (pval != scounts.end())
                    val = pval->second;
            }
            rates[j].push_back((double)val / (*it)->period);
        }
    }

    //chlog("times: " << times.size());
        
    pair<vector<double>,
         vector<vector<double> > > rtn = make_pair(times, rates);

    //chlog("Returning " << rtn.first.size() << " times");
        
    // Pack return value into msgpack buffer
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, rtn);
    //  Send reply back to client.
    zmq::message_t* reply = sbuffer_to_message(buffer);
    return _send_frontend_message(client, token, *reply);
}


int L1RpcServer::_handle_get_statistics(zmq::message_t& client, string funcname, uint32_t token,
                                        const char* req_data, size_t req_len, size_t& offset) {

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
    return _send_frontend_message(client, token, *reply);
}

int L1RpcServer::_handle_list_chunks(zmq::message_t& client, string funcname, uint32_t token,
                                     const char* req_data, size_t req_len, size_t& offset) {

    // No input arguments, so don't unpack anything more.

    // Grab snapshot of all ringbufs...
    vector<tuple<uint64_t, uint64_t, uint64_t, uint64_t> > allchunks;

    for (int beam : _stream->get_beam_ids()) {
        // Retrieve one beam at a time
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
    return _send_frontend_message(client, token, *reply);
                                      
}

int L1RpcServer::_handle_write_chunks(zmq::message_t& client, string funcname, uint32_t token,
                                      const char* req_data, size_t req_len, size_t& offset) {

    //cout << "RPC write_chunks() called" << endl;
    // grab WriteChunks_Request argument
    msgpack::object_handle oh = msgpack::unpack(req_data, req_len, offset);

    bool need_rfi = false;
        
    // "v1" write_chunks requests have 9 arguments; "v2" have 10 arguments ("need_rfi" flag added)
    msgpack::object obj = oh.get();
    cout << "Write_chunks request: arguments size " << obj.via.array.size << endl;
    WriteChunks_Request req;
    if (obj.via.array.size == 9) {
        req = oh.get().as<WriteChunks_Request>();
    } else if (obj.via.array.size == 10) {
        WriteChunks_Request_v2 req2 = oh.get().as<WriteChunks_Request_v2>();
        req = req2;
        need_rfi = req2.need_rfi;
    }

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
        w->chunk_status = this->_chunk_status;
        w->priority = req.priority;
        w->chunk = chunk;
        w->token = token;
        w->need_wait = need_rfi;

        // Returns false if request failed to queue.  
        bool success = _output_devices.enqueue_write_request(w);

        if (success)
            _update_n_chunks_waiting(true);

        // Record the status for this filename.
        // (status will be set via a status_changed() call during
        // enqueue_write_request.)
        string status;
        string error_message;
        _chunk_status->get(w->filename, status, error_message);

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

    return _send_frontend_message(client, token, *replymsg);
}

int L1RpcServer::_handle_masked_freqs(zmq::message_t& client, string funcname, uint32_t token,
                                      const char* req_data, size_t req_len, size_t& offset) {
    // no arguments?

    // Returns:
    // list of [beam_id, where, nt, [ measurements ] ]
    // where measurements is a list of uint16_ts, one per freq bin

    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> pk(&buffer);
    vector<int> beams = _stream->get_beam_ids();
    int ngood = 0;
    for (const auto &it : _mask_stats->map) {
        int stream_ind = it.first.first;
        if (stream_ind >= (int)beams.size()) {
            cout << "masked_freqs: first packet not received yet" << endl;
            continue;
        }
        ngood++;
    }
    pk.pack_array(ngood);
    for (const auto &it : _mask_stats->map) {
        int stream_ind = it.first.first;
        if (stream_ind >= (int)beams.size()) {
            cout << "masked_freqs: first packet not received yet" << endl;
            continue;
        }
        string where = it.first.second;
        int beam = beams[stream_ind];
        shared_ptr<rf_pipelines::mask_measurements_ringbuf> ms = it.second;
        vector<rf_pipelines::mask_measurements> meas = ms->get_all_measurements();
        cout << "Got " << meas.size() << " measurements from beam " << beam << " where " << where << endl;
        pk.pack_array(4);
        pk.pack(beam);
        pk.pack(where);
        int nt = 0;
        if (meas.size())
            nt = meas[0].nt;
        pk.pack(nt);
        pk.pack_array(meas.size());
        for (const auto &m : meas) {
            pk.pack_array(m.nf);
            int* fum = m.freqs_unmasked.get();
            for (int k=0; k<m.nf; k++)
                pk.pack(m.nt - fum[k]);
        }
    }
    //  Send reply back to client.
    zmq::message_t* reply = sbuffer_to_message(buffer);
    return _send_frontend_message(client, token, *reply);
}

string L1RpcServer::_handle_fork(bool start, const char* req_data, size_t req_size, size_t req_offset) {

    msgpack::object_handle oh = msgpack::unpack(req_data, req_size, req_offset);
    if (oh.get().via.array.size != 4) {
        chlog("fork_beam RPC: failed to parse input arguments");
        return "Failed to parse inputs (need 4)";
    }
    int beam = oh.get().via.array.ptr[0].as<int>();
    int dest_beam = oh.get().via.array.ptr[1].as<int>();
    string dest_ip = oh.get().via.array.ptr[2].as<string>();
    int dest_port = oh.get().via.array.ptr[3].as<int>();
    
    cout << "Forking: " << (start ? "start" : "stop") << " beam " << beam << " to dest " << dest_beam << ", dest IP " << dest_ip << " port " << dest_port << endl;
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    if (!inet_aton(dest_ip.c_str(), &(dest.sin_addr)))
        return "Failed to parse fork destination " + dest_ip;
    dest.sin_port = htons(dest_port);
    if (start)
        _stream->start_forking_packets(beam, dest_beam, dest);
    else 
        _stream->stop_forking_packets(beam, dest_beam, dest);
    return "";
}

string L1RpcServer::_handle_inject(const char* req_data, size_t req_size, size_t req_offset) {
    if (!_heavy || (_injectors.size() == 0))
        return "This RPC endoint does not support the inject_data call.  Try the heavy-weight RPC port.";

    // This struct defines the msgpack "wire protocol"
    shared_ptr<inject_data_request> injreq = make_shared<inject_data_request>();

    // This is the struct we will send to rf_pipelines
    shared_ptr<rf_pipelines::intensity_injector::inject_args> injdata = make_shared<rf_pipelines::intensity_injector::inject_args>();

    // And this is the injector (the rf_pipelines component where the data actually get injected)
    shared_ptr<rf_pipelines::intensity_injector> syringe;

    msgpack::object_handle oh = msgpack::unpack(req_data, req_size, req_offset);
    msgpack::object obj = oh.get();
    obj.convert(injreq);
    int beam = injreq->beam;
    uint64_t fpga0 = injreq->fpga0;
    injreq->swap(*injdata);
    injreq.reset();

    assert(_stream.get());
    string errstring = injdata->check(_stream->ini_params.nupfreq * ch_frb_io::constants::nfreq_coarse_tot);
    if (errstring.size())
        return errstring;

    // find the injector for this beam
    syringe = _get_injector_for_beam(beam);
    if (!syringe) {
        string bstr = "";
        for (int b : _stream->get_beam_ids())
	    bstr += (bstr.size() ? ", " : "") + to_string(b);
        return "inject_data: beam=" + to_string(beam) + " which is not a beam that I am handling ("
	  + bstr + ")";
    }
    chlog("injection request matched beam " << beam << "; injector is " << syringe.get());

    // rf_pipelines sample offset
    uint64_t stream_fpga0;
    try {
        // If the first packet has not been received, we don't know FPGA0.
        stream_fpga0 = _stream->get_first_fpgacount();
    } catch (const runtime_error &e) {
        return e.what();
    }
    injdata->sample0 = (ssize_t)(fpga0 / (uint64_t)_stream->ini_params.fpga_counts_per_sample) -
        (ssize_t)(stream_fpga0 / (uint64_t)_stream->ini_params.fpga_counts_per_sample);
    //cout << "L1-RPC inject_data: injection FPGA0 " << fpga0 << ", stream's first FPGA count: " << _stream->get_first_fpga_count(beam) << ", sample0 " << injdata->sample0 << endl;

    try {
        syringe->inject(injdata);
    } catch (const runtime_error &e) {
        return e.what();
    }
    chlog("Inject_data RPC: beam " << beam << ", mode " << injdata->mode << ", nfreq " << injdata->sample_offset.size() << ", sample0 " << injdata->sample0 << ", offset range [" << injdata->min_offset << ", " << injdata->max_offset << "], vs current stream sample " << syringe->pos_lo << " -> this injection will happen " << ((injdata->sample0 + injdata->min_offset - syringe->pos_lo)/1024) << " to " << ((injdata->sample0 + injdata->max_offset - syringe->pos_lo)/1024) << " seconds in the future.");
    return "";
}

int L1RpcServer::_handle_masked_freqs_2(zmq::message_t& client, string funcname, uint32_t token,
                                        const char* req_data, size_t req_len, size_t& offset) {
    // arguments: [beam, where, fpgacounts_low, fpgacounts_high]
    // If *beam* == -1, returns all beams
    // *where*: you probably want "before_rfi" or "after_rfi".
    msgpack::object_handle oh = msgpack::unpack(req_data, req_len, offset);
    if (oh.get().via.array.size != 4) {
        chlog("get_masked_frequencies_2: expected arguments (beam, where, fpgacounts_low, fpgacounts_high)");
        return -1;
    }
    int beam           = oh.get().via.array.ptr[0].as<int>();
    string where       = oh.get().via.array.ptr[1].as<string>();
    uint64_t fpgastart = oh.get().via.array.ptr[2].as<uint64_t>();
    uint64_t fpgaend   = oh.get().via.array.ptr[3].as<uint64_t>();

    // Returns:
    // list (one per beam) of:
    // [beam_id, fpga_start, fpga_end, pos_start, nt, nf, nsamples,
    //  nsamples_masked, FREQS_MASKED]
    // here FREQS_MASKED is a list (array / histogram) of length "nf".
    // "pos_start" and "nt" are in intensity samples.

    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> pk(&buffer);

    vector<shared_ptr<rf_pipelines::mask_measurements> > measlist;
    vector<int> measbeams;
    vector<uint64_t> measfpga0;

    uint64_t fpga0;
    try {
        fpga0 = _stream->get_first_fpgacount();
    } catch (const std::exception& e) {
        chlog("get_masked_frequencies_2: no fpga0");
        pk.pack_array(0);
        //  Send reply back to client.
        zmq::message_t* reply = sbuffer_to_message(buffer);
        return _send_frontend_message(client, token, *reply);
    }

    uint64_t fpga_counts_per_sample = _stream->ini_params.fpga_counts_per_sample;
    vector<int> beams = _stream->get_beam_ids();

    for (const auto &it : _mask_stats->map) {
        int stream_ind = it.first.first;
        string this_where = it.first.second;

        // match requested "beam" & "where"
        if (beam >= 0 && beams[stream_ind] != beam)
            continue;
        if (this_where != where)
            continue;

        ssize_t samplestart = 0;
        ssize_t sampleend   = 0;
        if (fpgastart >= fpga0)
            samplestart = (fpgastart - fpga0) / fpga_counts_per_sample;
        if (fpgaend >= fpga0)
            sampleend   = (fpgaend   - fpga0) / fpga_counts_per_sample;
        shared_ptr<rf_pipelines::mask_measurements_ringbuf> ms = it.second;
        shared_ptr<rf_pipelines::mask_measurements> meas = ms->get_summed_measurements(samplestart, sampleend);
        if (!meas) {
            //chlog("Got empty measurement");
        } else {
            //chlog("Got summed measurements: pos " << meas->pos << ", nt " << meas->nt);
            measlist.push_back(meas);
            measbeams.push_back(beams[stream_ind]);
            measfpga0.push_back(fpga0);
        }
    }

    pk.pack_array(measlist.size());
    for (size_t i=0; i<measlist.size(); i++) {
        shared_ptr<rf_pipelines::mask_measurements> meas = measlist[i];
        int beam_id = measbeams[i];
        uint64_t fpga0 = measfpga0[i];
        pk.pack_array(9);
        pk.pack(beam_id);
        pk.pack(fpga0 + meas->pos * fpga_counts_per_sample);
        pk.pack(fpga0 + (meas->pos + meas->nt) * fpga_counts_per_sample);
        pk.pack(meas->pos);
        pk.pack(meas->nt);
        pk.pack(meas->nf);
        pk.pack(meas->nsamples);
        pk.pack(meas->nsamples_unmasked);
        pk.pack_array(meas->nf);
        int* fum = meas->freqs_unmasked.get();
        for (int k=0; k<meas->nf; k++)
            pk.pack(meas->nt - fum[k]);
    }
    //  Send reply back to client.
    zmq::message_t* reply = sbuffer_to_message(buffer);
    return _send_frontend_message(client, token, *reply);
    
}

int L1RpcServer::_handle_max_fpga(zmq::message_t& client, string funcname, uint32_t token,
                                  const char* req_data, size_t req_len, size_t& offset) {
    // Returns a list of tuples:
    // [(where, beam, max_fpgacount_seen), ...]
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> pk(&buffer);

    vector<fpga_counts_seen> fpgaseen;
    fpga_counts_seen seen;

    seen.where = "packet_stream";
    seen.beam = -1;
    seen.max_fpga_seen = _stream->packet_max_fpga_seen;
    fpgaseen.push_back(seen);

    vector<uint64_t> flushed;
    vector<uint64_t> retrieved;
    _stream->get_max_fpga_count_seen(flushed, retrieved);

    vector<int> beams = _stream->get_beam_ids();
    for (size_t i=0; i<beams.size(); i++) {
        seen.beam = beams[i];
        seen.where = "chunk_flushed";
        seen.max_fpga_seen = flushed[i];
        fpgaseen.push_back(seen);

        seen.where = "chunk_retrieved";
        seen.max_fpga_seen = retrieved[i];
        fpgaseen.push_back(seen);
    }

    uint64_t fpga0 = 0;
    try {
        fpga0 = _stream->get_first_fpgacount();
    } catch (const runtime_error &e) {
        // if first packet has not been received
        goto bailout;
    }
    for (size_t i=0; i<_latencies.size(); i++) {
        int stream_ibeam = std::get<0>(_latencies[i]);
        string where = std::get<1>(_latencies[i]);
        const auto &late = std::get<2>(_latencies[i]);
        int beam_id = _stream->get_beam_ids()[stream_ibeam];
        uint64_t fpga = (late->pos_lo * _stream->ini_params.fpga_counts_per_sample
                         + fpga0);
        seen.beam = beam_id;
        seen.where = where;
        seen.max_fpga_seen = fpga;
        fpgaseen.push_back(seen);
    }

    for (auto iter = _beam_to_bonsai.begin(); iter != _beam_to_bonsai.end(); iter++) {
        int beam_id = iter->first;
        auto bonsai = iter->second;
        int nc = bonsai->get_nchunks_processed();
        uint64_t fpga = (nc * bonsai->nt_chunk * _stream->ini_params.fpga_counts_per_sample
                         + fpga0);
        seen.beam = beam_id;
        seen.where = "bonsai";
        seen.max_fpga_seen = fpga;
        fpgaseen.push_back(seen);
    }

 bailout:
    pk.pack(fpgaseen);

    //  Send reply back to client.
    zmq::message_t* reply = sbuffer_to_message(buffer);
    return _send_frontend_message(client, token, *reply);
    
}

// Called periodically by the RpcServer thread.
void L1RpcServer::_check_backend_queue()
{
    for (;;) {
	shared_ptr<l1_backend_queue::entry> w = _backend_queue->dequeue_write_reply();
	if (!w)
	    return;

        // Waiting for one fewer chunk!
        _update_n_chunks_waiting(false);

	if (!w->client)
	    continue;
	// We received a WriteChunks_Reply from the I/O thread pool!
	// Forward the reply to the client, if requested.
	const WriteChunks_Reply &rep = w->reply;

	zmq::message_t client;
        client.copy(*(w->client));

	// Format the reply sent to the client.
	msgpack::sbuffer buffer;
	msgpack::pack(buffer, rep);
	zmq::message_t* reply = sbuffer_to_message(buffer);
	_send_frontend_message(client, w->token, *reply);
	delete reply;
    }
}

int L1RpcServer::_send_frontend_message(zmq::message_t& clientmsg,
                                        uint32_t token,
                                        zmq::message_t& contentmsg) {
    return _send_frontend_message(clientmsg, *token_to_message(token), contentmsg);
}

int L1RpcServer::_send_frontend_message(zmq::message_t& clientmsg,
                                        zmq::message_t& tokenmsg,
                                        zmq::message_t& contentmsg) {
    const zmq::send_flags more = zmq::send_flags::sndmore;
    try {
        if (!(_frontend.send(clientmsg, more) &&
              _frontend.send(tokenmsg, more) &&
              _frontend.send(contentmsg, zmq::send_flags::none))) {
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

void L1RpcServer::_update_n_chunks_waiting(bool inc) {
    if (inc) {
        if (_n_chunks_writing == 0) {
            cout << "Writing chunks.  Pausing packet forking." << endl;
            _stream->pause_forking_packets();
        }
        _n_chunks_writing++;
    } else {
        _n_chunks_writing--;
        if (_n_chunks_writing == 0) {
            cout << "Finished writing chunks.  Resuming packet forking." << endl;
            _stream->resume_forking_packets();
        }
    }
}

