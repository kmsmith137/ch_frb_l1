#include <unistd.h>
#include <iostream>
#include "ch_frb_io.hpp"
#include "ringbuf.hpp"
using namespace ch_frb_io;
using namespace std;

std::ostream& operator<<(std::ostream& s, const assembled_chunk& ch) {
    s << "assembled_chunk(beam " << ch.beam_id << ", ichunk " << ch.ichunk << " at " << (void*)(&ch) << ")";
    return s;
}

class L1Ringbuf;

class AssembledChunkRingbuf : public Ringbuf<assembled_chunk> {

public:
    AssembledChunkRingbuf(int binlevel, L1Ringbuf* parent, int maxsize) :
        Ringbuf<assembled_chunk>(maxsize),
        _binlevel(binlevel),
        _parent(parent)
    {}

    virtual ~AssembledChunkRingbuf() {}

protected:
    // my time-binning. level: 0 = original intensity stream; 1 =
    // binned x 2, 2 = binned x 4.
    int _binlevel;
    L1Ringbuf* _parent;

    // Called when the given frame *t* is being dropped off the buffer
    // to free up some space for a new frame.
    virtual void dropping(shared_ptr<assembled_chunk> t);

};

static bool
assembled_chunk_overlaps_range(const shared_ptr<assembled_chunk> ch, 
                               uint64_t min_fpga_counts,
                               uint64_t max_fpga_counts) {
    if (min_fpga_counts == 0 && max_fpga_counts == 0)
        return true;
    uint64_t fpga0 = ch->isample * ch->fpga_counts_per_sample;
    uint64_t fpga1 = fpga0 + constants::nt_per_assembled_chunk * ch->fpga_counts_per_sample;
    cout << "Chunk FPGA counts range " << fpga0 << " to " << fpga1 << endl;
    if ((max_fpga_counts && (fpga0 > max_fpga_counts)) ||
        (min_fpga_counts && (fpga1 < min_fpga_counts)))
        return false;
    return true;
}

class L1Ringbuf {
    friend class AssembledChunkRingbuf;

    static const size_t Nbins = 4;

public:
    L1Ringbuf(uint64_t beam_id) :
        _beam_id(beam_id),
        _q(),
        _rb(),
        _dropped()
    {
        // Create the ring buffer objects for each time binning
        // (0 = native rate, 1 = binned by 2, ...)
        for (size_t i=0; i<Nbins; i++)
            _rb.push_back(shared_ptr<AssembledChunkRingbuf>
                          (new AssembledChunkRingbuf(i, this, 4)));
        // Fill the "_dropped" array with empty shared_ptrs.
        for (size_t i=0; i<Nbins-1; i++)
            _dropped.push_back(shared_ptr<assembled_chunk>());
    }

    /*
     Tries to enqueue an assembled_chunk.  If no space can be
     allocated, returns false.  The ring buffer now assumes ownership
     of the assembled_chunk.
     */
    bool push(assembled_chunk* ch) {
        shared_ptr<assembled_chunk> p = _rb[0]->push(ch);
        if (!p)
            return false;
        _q.push_back(p);
        return true;
    }

    /*
     Returns the next assembled_chunk for downstream processing.
     */
    shared_ptr<assembled_chunk> pop() {
        if (_q.empty())
            return shared_ptr<assembled_chunk>();
        shared_ptr<assembled_chunk> p = _q.front();
        _q.pop_front();
        return p;
    }

    /*
     Prints a report of the assembled_chunks currently queued.
     */
    void print() {
        cout << "L1 ringbuf:" << endl;
        cout << "  downstream: [ ";
        for (auto it = _q.begin(); it != _q.end(); it++) {
            cout << (*it)->ichunk << " ";
        }
        cout << "];" << endl;
        for (size_t i=0; i<Nbins; i++) {
            vector<shared_ptr<assembled_chunk> > v = _rb[i]->snapshot(NULL);
            cout << "  binning " << i << ": [ ";
            for (auto it = v.begin(); it != v.end(); it++) {
                cout << (*it)->ichunk << " ";
            }
            cout << "]" << endl;
            if (i < Nbins-1) {
                cout << "  dropped " << i << ": ";
                if (_dropped[i])
                    cout << _dropped[i]->ichunk << endl;
                else
                    cout << "none" << endl;
            }
        }
    }

    void retrieve(uint64_t min_fpga_counts, uint64_t max_fpga_counts,
                  vector<shared_ptr<assembled_chunk> >& chunks) {
        // Check downstream queue
        cout << "Retrieve: checking downstream queue" << endl;
        for (auto it = _q.begin(); it != _q.end(); it++) {
            if (assembled_chunk_overlaps_range(*it, min_fpga_counts, max_fpga_counts)) {
                cout << "  got: " << *(*it) << endl;
                chunks.push_back(*it);
            }
        }
        for (size_t i=0; i<Nbins; i++) {
            cout << "Retrieve: binning " << i << endl;
            size_t size0 = chunks.size();
            _rb[i]->snapshot(chunks, std::bind(assembled_chunk_overlaps_range, placeholders::_1, min_fpga_counts, max_fpga_counts));
            size_t size1 = chunks.size();
            for (size_t j=size0; j<size1; j++)
                cout << "  got: " << *(chunks[j]) << endl;

            if ((i < Nbins-1) && (_dropped[i])) {
                cout << "Checking dropped chunk at level " << i << endl;
                if (assembled_chunk_overlaps_range(_dropped[i], min_fpga_counts, max_fpga_counts)) {
                    chunks.push_back(_dropped[i]);
                    cout << "  got: " << *(_dropped[i]) << endl;
                }
            }
        }

        
    }

public:
    uint64_t _beam_id;
    
protected:
    // The queue for downstream
    deque<shared_ptr<assembled_chunk> > _q;

    // The ring buffers for each time-binning.  Length fixed at Nbins.
    vector<shared_ptr<AssembledChunkRingbuf> > _rb;

    // The assembled_chunks that have been dropped from the ring
    // buffers and are waiting for a pair to be time-downsampled.
    // Length fixed at Nbins-1.
    vector<shared_ptr<assembled_chunk> > _dropped;

    // Called from the AssembledChunkRingbuf objects when a chunk is
    // about to be dropped from one binning level of the ringbuf.  If
    // the chunk does not have a partner waiting (in _dropped), then
    // it is saved in _dropped.  Otherwise, the two chunks are merged
    // into one new chunk and added to the next binning level's
    // ringbuf.
    void dropping(int binlevel, shared_ptr<assembled_chunk> ch) {
        cout << "Bin level " << binlevel << " dropping a chunk" << endl;
        if (binlevel >= (int)(Nbins-1))
            return;

        if (_dropped[binlevel]) {
            cout << "Now have 2 dropped chunks from bin level " << binlevel << endl;
            // FIXME -- bin down
            assembled_chunk* binned = new assembled_chunk(ch->beam_id, ch->nupfreq, ch->nt_per_packet, ch->fpga_counts_per_sample, _dropped[binlevel]->ichunk);
            // push onto _rb[level+1]
            cout << "Pushing onto level " << (binlevel+1) << endl;
            _rb[binlevel+1]->push(binned);
            cout << "Dropping shared_ptr..." << endl;
            _dropped[binlevel].reset();
            cout << "Done dropping" << endl;
        } else {
            // Keep this one until its partner arrives!
            cout << "Saving as _dropped" << binlevel << endl;
            _dropped[binlevel] = ch;
        }
    }

};

// after L1Ringbuf has been declared...
void AssembledChunkRingbuf::dropping(shared_ptr<assembled_chunk> t) {
    _parent->dropping(_binlevel, t);
}




#include <zmq.hpp>
#include <msgpack.hpp>
#include <pthread.h>

#include "rpc.hpp"

static string msg_string(zmq::message_t &msg) {
    return string(static_cast<const char*>(msg.data()), msg.size());
}

// RPC multi-threaded server, structure from
// http://zguide.zeromq.org/cpp:asyncsrv

struct write_chunk_request {
    zmq::message_t* client;
    string filename;
    int priority;
    shared_ptr<assembled_chunk> chunk;
};

class RpcServer {
public:
    RpcServer(zmq::context_t &ctx, string port,
              vector<shared_ptr<L1Ringbuf> > ringbufs);
    ~RpcServer();
    write_chunk_request pop_write_request();
    void run();

protected:
    int _handle_request(zmq::message_t* client, zmq::message_t* request);
    void _add_write_request(write_chunk_request &req);
    void _get_chunks(vector<uint64_t> &beams,
                     uint64_t min_fpga, uint64_t max_fpga,
                     vector<shared_ptr<assembled_chunk> > &chunks);
private:
    zmq::context_t &_ctx;
    zmq::socket_t _frontend;
    zmq::socket_t _backend;
    string _port;

    // the queue of write requests to be run by the RpcWorker(s)
    deque<write_chunk_request> _write_reqs;
    // (and the mutex for it)
    pthread_mutex_t _q_lock;

    vector<shared_ptr<L1Ringbuf> > _ringbufs;
};

static void myfree(void* p, void*) {
    ::free(p);
}

static zmq::message_t sbuffer_to_message(msgpack::sbuffer &buffer) {
    zmq::message_t msg(buffer.data(), buffer.size(), myfree);
    buffer.release();
    return msg;
}

class RpcWorker {
public:
    RpcWorker(zmq::context_t* ctx, RpcServer* server) :
        _socket(*ctx, ZMQ_DEALER),
        _server(server) {
    }

    void run() {
        _socket.connect("inproc://rpc-backend");

        while (true) {
            zmq::message_t msg;
            _socket.recv(&msg);
            // Expect empty message from RpcServer

            // Pull a write_chunk_request off the queue!
            write_chunk_request w = _server->pop_write_request();

            cout << "Worker got write request: beam " << w.chunk->beam_id << ", chunk " << w.chunk->ichunk << ", FPGA counts " << (w.chunk->isample * w.chunk->fpga_counts_per_sample) << endl;

            WriteChunks_Reply rep;
            rep.beam = w.chunk->beam_id;
            rep.chunk = w.chunk->ichunk;
            rep.success = false;
            rep.filename = w.filename;

            try {
                cout << "write_msgpack_file: " << w.filename << endl;
                w.chunk->msgpack_bitshuffle = true;
                w.chunk->write_msgpack_file(w.filename);
                cout << "write_msgpack_file succeeded" << endl;
                rep.success = true;
            } catch (...) {
                cout << "Write msgpack file failed." << endl;
                rep.error_message = "Failed to write msgpack file";
            }
            // Drop this chunk so its memory can be reclaimed
            w.chunk.reset();

            msgpack::sbuffer buffer;
            msgpack::pack(buffer, rep);
            /*
            //zmq::message_t reply(buffer.data(), buffer.size());
             zmq::message_t reply(buffer.data(), buffer.size(), myfree);
             buffer.release();
            zmq::message_t reply;
            sbuffer_to_message(buffer, reply);
             */
            zmq::message_t reply = sbuffer_to_message(buffer);


            // DEBUG
            cout << "RpcWorker: sleeping..." << endl;
            usleep(1000000);

            //cout << "Worker: sending reply for client " << msg_string(*(w.client)) << " and message " << reply.size() << " bytes" << endl;
            _socket.send(*w.client, ZMQ_SNDMORE);
            _socket.send(reply);
            delete w.client;
        }
    }

private:
    zmq::socket_t _socket;
    RpcServer* _server;
};

struct rpc_worker_thread_context {
    zmq::context_t* ctx;
    RpcServer* server;
};

static void* rpc_worker_thread_main(void *opaque_arg) {
    rpc_worker_thread_context *context = reinterpret_cast<rpc_worker_thread_context *>(opaque_arg);
    zmq::context_t* ctx = context->ctx;
    RpcServer* server = context->server;
    delete context;

    RpcWorker rpc(ctx, server);
    rpc.run();
    return NULL;
}

RpcServer::RpcServer(zmq::context_t &ctx, string port,
                     vector<shared_ptr<L1Ringbuf> > ringbufs) :
    _ctx(ctx),
    _frontend(_ctx, ZMQ_ROUTER),
    _backend(_ctx, ZMQ_DEALER),
    _port(port),
    _ringbufs(ringbufs)
{
    pthread_mutex_init(&this->_q_lock, NULL);
}

RpcServer::~RpcServer() {
    pthread_mutex_destroy(&this->_q_lock);
}

write_chunk_request RpcServer::pop_write_request() {
    write_chunk_request wreq;
    pthread_mutex_lock(&this->_q_lock);
    if (_write_reqs.empty())
        throw runtime_error(string("pop_write_request(): queue is empty"));
    wreq = _write_reqs.front();
    _write_reqs.pop_front();
    pthread_mutex_unlock(&this->_q_lock);
    return wreq;
}

void RpcServer::run() {
    cout << "RpcServer::run()" << endl;
    _frontend.bind(_port);
    _backend.bind("inproc://rpc-backend");

    std::vector<pthread_t*> worker_threads;
        
    // How many threads are writing to disk at once?
    int nworkers = 2;

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
        pthread_mutex_lock(&this->_q_lock);
        size_t n = _write_reqs.size();
        pthread_mutex_unlock(&this->_q_lock);

        cout << "RpcServer: polling.  Queued chunks to write: " << n << endl;

        //int r = zmq::poll(pollitems, 2, -1);
        int r = zmq::poll(pollitems, 2, 5000);
        if (r == -1) {
            cout << "zmq::poll error: " << strerror(errno) << endl;
            break;
        }

        zmq::message_t client;
        zmq::message_t msg;
        
        if (pollitems[0].revents & ZMQ_POLLIN) {
            cout << "Received message from client" << endl;
            _frontend.recv(&client);
            cout << "Client: " << msg_string(client) << endl;
            _frontend.recv(&msg);
            _handle_request(&client, &msg);
        }
        if (pollitems[1].revents & ZMQ_POLLIN) {
            cout << "Received reply from worker" << endl;
            int more;
            if (_backend.recv(&client) == 0) {
                cout << "Received zero bytes!" << endl;
                continue;
            }
            cout << "  client: " << msg_string(client) << endl;
            more = _backend.getsockopt<int>(ZMQ_RCVMORE);
            assert(more);
            if (_backend.recv(&msg) == 0) {
                cout << "Received zero bytes!" << endl;
                continue;
            }
            cout << "message: " << msg.size() << " bytes" << endl;
            more = _backend.getsockopt<int>(ZMQ_RCVMORE);
            assert(!more);

            /*
             msgpack::object_handle oh =
             msgpack::unpack(reinterpret_cast<const char *>(msg.data()), msg.size());
             msgpack::object obj = oh.get();
             cout << "  message: " << obj << endl;
             */
            _frontend.send(client, ZMQ_SNDMORE);
            _frontend.send(msg);
        }
    }

    // FIXME -- join threads?
    for (int i=0; i<nworkers; i++) {
        delete worker_threads[i];
    }
}

int RpcServer::_handle_request(zmq::message_t* client, zmq::message_t* request) {
    const char* req_data = reinterpret_cast<const char *>(request->data());
    std::size_t offset = 0;

    // Unpack the function name (string)
    msgpack::object_handle oh = msgpack::unpack(req_data, request->size(), offset);
    string funcname = oh.get().as<string>();

    // RPC reply
    msgpack::sbuffer buffer;

    if (funcname == "get_beam_metadata") {
        cout << "RPC get_beam_metadata() called" << endl;
        // No input arguments, so don't unpack anything more
        /*
        // FIXME
        std::vector<
            std::unordered_map<std::string, uint64_t> > R =
            stream->get_statistics();
        msgpack::pack(buffer, R);
         */

        //  Send reply back to client
        cout << "Sending RPC reply of size " << buffer.size() << endl;

        zmq::message_t reply = sbuffer_to_message(buffer);
        //zmq::message_t client_copy;
        //client_copy.copy(client);
        //int nsent = _frontend.send(client_copy, ZMQ_SNDMORE);
        if (!(_frontend.send(client, ZMQ_SNDMORE) &&
              _frontend.send(reply))) {
            cout << "ERROR: sending RPC reply: " << strerror(zmq_errno()) << endl;
            return -1;
        }
        return 0;

        /*
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
        cout << "RPC write_chunks() called" << endl;

        // grab WriteChunks_Request argument
        msgpack::object_handle oh = msgpack::unpack(req_data, request->size(), offset);
        WriteChunks_Request req = oh.get().as<WriteChunks_Request>();

        cout << "WriteChunks request: FPGA range " << req.min_fpga << "--" << req.max_fpga << endl;
        cout << "beams: [ ";
        for (auto beamit = req.beams.begin(); beamit != req.beams.end(); beamit++)
            cout << (*beamit) << " ";
        cout << "]" << endl;

        vector<shared_ptr<assembled_chunk> > chunks;
        _get_chunks(req.beams, req.min_fpga, req.max_fpga, chunks);
        cout << "get_chunks: got " << chunks.size() << " chunks" << endl;

        // FIXME -- should we send back an immediate status reply with the
        // list of chunks & filenames to be written??

        //vector<WriteChunks_Reply> rtn;
        for (auto chunk = chunks.begin(); chunk != chunks.end(); chunk++) {
            // WriteChunks_Reply rep;
            // rep.beam = (*chunk)->beam_id;
            // rep.chunk = (*chunk)->ichunk;
            // rep.success = false;
            // cout << "Writing chunk for beam " << rep.beam << ", chunk " << rep.chunk << endl;
            write_chunk_request w;
            char* strp = NULL;
            int r = asprintf(&strp, req.filename_pattern.c_str(), (*chunk)->beam_id, (*chunk)->ichunk);
            if (r == -1) {
                //rep.error_message = "asprintf failed to format filename";
                //rtn.push_back(rep);
                cout << "Failed to format filename: " << req.filename_pattern << endl;
                continue;
            }
            w.filename = string(strp);
            free(strp);
            // rep.filename = filename;
            // rep.success = true;
            // rtn.push_back(rep);

            // Copy client ID
            w.client = new zmq::message_t(client->data(), client->size());
            w.priority = req.priority;
            w.chunk = *chunk;
            _add_write_request(w);
        }
        //msgpack::pack(buffer, rtn);
        return 0;
    } else {
        // Silent failure?
        cout << "Error: unknown RPC function name: " << funcname << endl;
        return -1;
    }
}
        
void RpcServer::_add_write_request(write_chunk_request &req) {
    pthread_mutex_lock(&this->_q_lock);
    // FIXME -- priority queue; merge requests for same chunk.

    // Highest priority goes at the front of the queue.
    // Search for the first element with priority lower than this one's.
    // AND search for a higher-priority duplicate of this element.
    deque<write_chunk_request>::iterator it;
    for (it = _write_reqs.begin(); it != _write_reqs.end(); it++) {
        if (it->chunk == req.chunk) {
            cout << "Found an existing write request for chunk: beam " << req.chunk->beam_id << ", ichunk " << req.chunk->ichunk << " with >= priority" << endl;
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
    it++;
    for (; it != _write_reqs.end(); it++) {
        if (it->chunk == req.chunk) {
            cout << "Found existing write request for this chunk with priority " << it->priority << " vs " << req.priority << endl;
            _write_reqs.erase(it);
            // There can only be one existing copy of this chunk, so we're done.
            // Mark that we have not added more work overall.
            added = false;
            break;
        }
    }

    cout << "Added write request: now " << _write_reqs.size() << " queued" << endl;
    cout << "Queue:" << endl;
    for (it = _write_reqs.begin(); it != _write_reqs.end(); it++) {
        cout << "  priority " << it->priority << ", chunk " << *(it->chunk) << endl;
    }
    pthread_mutex_unlock(&this->_q_lock);
    if (added)
        // Send (empty) message to backend workers to trigger doing work.
        _backend.send(NULL, 0);
}

void RpcServer::_get_chunks(vector<uint64_t> &beams,
                            uint64_t min_fpga, uint64_t max_fpga,
                            vector<shared_ptr<assembled_chunk> > &chunks) {
    cout << "_get_chunks: checking " << _ringbufs.size() << " ring buffers" << endl;
    for (auto it = _ringbufs.begin(); it != _ringbufs.end(); it++) {
        for (auto beamit = beams.begin(); beamit != beams.end(); beamit++) {
            if (*beamit != (*it)->_beam_id)
                continue;
            (*it)->retrieve(min_fpga, max_fpga, chunks);
            break;
        }
    }
}

struct rpc_thread_contextX {
    vector<shared_ptr<L1Ringbuf> > ringbufs;
    // eg, "tcp://*:5555";
    string port;
};

static void *rpc_thread_main(void *opaque_arg) {
    rpc_thread_contextX *context = reinterpret_cast<rpc_thread_contextX *> (opaque_arg);
    string port = context->port;
    vector<shared_ptr<L1Ringbuf> > ringbufs = context->ringbufs;
    delete context;

    zmq::context_t ctx;
    RpcServer rpc(ctx, port, ringbufs);
    rpc.run();
    return NULL;
}

void rpc_server_start(string port, vector<shared_ptr<L1Ringbuf> > ringbufs) {
    cout << "Starting RPC server on " << port << endl;

    rpc_thread_contextX *context = new rpc_thread_contextX;
    context->ringbufs = ringbufs;
    context->port = port;

    pthread_t* rpc_thread = new pthread_t;
    int err = pthread_create(rpc_thread, NULL, rpc_thread_main, context);
    if (err)
        throw runtime_error(string("pthread_create() failed to create RPC thread: ") + strerror(errno));
}

int main() {

    int beam = 77;

    vector<shared_ptr<L1Ringbuf> > ringbufs;
    shared_ptr<L1Ringbuf> rb(new L1Ringbuf(beam));
    ringbufs.push_back(rb);
    rpc_server_start("tcp://127.0.0.1:5555", ringbufs);

    int nupfreq = 4;
    int nt_per = 16;
    int fpga_per = 400;

    assembled_chunk* ch;

    std::random_device rd;
    std::mt19937 rng(rd());
    rng.seed(42);
    std::uniform_int_distribution<> rando(0,1);

    for (int i=0; i<100; i++) {
        ch = new assembled_chunk(beam, nupfreq, nt_per, fpga_per, i);
        cout << "Pushing " << i << endl;
        rb->push(ch);

        cout << "Pushed " << i << endl;
        rb->print();
        cout << endl;

        // downstream thread consumes with a lag of 2...
        if (i >= 2) {
            // Randomly consume 0 to 2 chunks
            if (rando(rng)) {
                cout << "Downstream consumes a chunk" << endl;
                rb->pop();
            }
            if (rando(rng)) {
                cout << "Downstream consumes a chunk" << endl;
                rb->pop();
            }
        }
    }

    cout << "End state:" << endl;
    rb->print();
    cout << endl;

    vector<shared_ptr<assembled_chunk> > chunks;
    cout << "Retrieving chunks..." << endl;
    rb->retrieve(30000000, 50000000, chunks);
    cout << "Got " << chunks.size() << endl;

    usleep(30 * 1000000);
}


/*
int main() {
    cout << "Creating ringbuf..." << endl;
    Ringbuf<int> rb(4);

    int a = 42;
    int b = 43;
    int c = 44;

    cout << "Pushing" << endl;
    rb.push(&a);
    cout << "Pushing" << endl;
    rb.push(&b);
    cout << "Pushing" << endl;
    rb.push(&c);

    cout << "Popping" << endl;
    shared_ptr<int> p1 = rb.pop();
    cout << "Popping" << endl;
    shared_ptr<int> p2 = rb.pop();
    cout << "Dropping" << endl;
    p1.reset();
    cout << endl;

    int d = 45;
    int e = 46;
    int f = 47;
    int g = 48;

    cout << "Pushing d..." << endl;
    shared_ptr<int> pd = rb.push(&d);

    cout << endl;
    cout << "Pushing e..." << endl;
    shared_ptr<int> pe = rb.push(&e);

    cout << endl;
    cout << "Pushing f..." << endl;
    shared_ptr<int> pf = rb.push(&f);

    cout << endl;
    cout << "Pushing g..." << endl;
    rb.push(&g);

    cout << "Done" << endl;

}
 */

