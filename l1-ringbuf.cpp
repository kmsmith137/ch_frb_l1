#include <iostream>
#include "ch_frb_io.hpp"
#include "ringbuf.hpp"
using namespace ch_frb_io;
using namespace std;

std::ostream& operator<<(std::ostream& s, const assembled_chunk& ch) {
    s << "assembled_chunk(beam " << ch.beam_id << ", ichunk " << ch.ichunk << ")";
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
    uint64_t fpga0 = ch->isample * ch->fpga_counts_per_sample;
    uint64_t fpga1 = fpga0 + constants::nt_per_assembled_chunk * ch->fpga_counts_per_sample;
    cout << "Chunk FPGA counts range " << fpga0 << " to " << fpga1 << endl;
    if ((fpga0 > max_fpga_counts) || (fpga1 < min_fpga_counts))
        return false;
    return true;
}

class L1Ringbuf {
    friend class AssembledChunkRingbuf;

    static const size_t Nbins = 4;

public:
    L1Ringbuf() :
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
                chunks.push_back(*it);
            }
        }
        for (size_t i=0; i<Nbins; i++) {
            cout << "Retrieve: binning " << i << endl;
            _rb[i]->snapshot(chunks, std::bind(assembled_chunk_overlaps_range, placeholders::_1, min_fpga_counts, max_fpga_counts));

            if ((i < Nbins-1) && (_dropped[i])) {
                cout << "Checking dropped chunk at level " << i << endl;
                if (assembled_chunk_overlaps_range(_dropped[i], min_fpga_counts, max_fpga_counts)) {
                    chunks.push_back(_dropped[i]);
                }
            }
        }

        
    }
    
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
#include <pthread.h>

// RPC multi-threaded server, from
// http://zguide.zeromq.org/cpp:asyncsrv

class RpcWorker {
public:
    RpcWorker(zmq::context_t* ctx) ://, int sock_type) :
        //_ctx(ctx),
        _socket(*ctx, ZMQ_DEALER) {
    }

    void run() {
        _socket.connect("inproc://rpc-backend");

        while (true) {
            zmq::message_t client;
            zmq::message_t msg;
            _socket.recv(&client);
            _socket.recv(&msg);
            //zmq::message_t copied_id;
            //zmq::message_t copied_msg;
            //copied_id.copy(&identity);
            //copied_msg.copy(&msg);
            // FIXME
            _socket.send(client, ZMQ_SNDMORE);
            _socket.send(msg);
            //zmq::message_t reply;
            //_socket.send(reply);
        }
    }

private:
    //zmq::context_t &_ctx;
    zmq::socket_t _socket;
};

struct rpc_worker_thread_context {
    zmq::context_t* ctx;
};

static void* rpc_worker_thread_main(void *opaque_arg) {
    rpc_worker_thread_context *context = reinterpret_cast<rpc_worker_thread_context *>(opaque_arg);
    //shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    //string port = context->port;
    zmq::context_t* ctx = context->ctx;
    delete context;

    RpcWorker rpc(ctx);
    rpc.run();
    return NULL;
}


class RpcServer {
public:
    RpcServer(zmq::context_t &ctx, string port) :
        _ctx(ctx),
        _frontend(_ctx, ZMQ_ROUTER),
        _backend(_ctx, ZMQ_DEALER),
        _port(port)
    {}

    void run() {
        cout << "RpcServer::run()" << endl;
        _frontend.bind(_port);
        _backend.bind("inproc://rpc-backend");

        //std::vector<RpcWorker*> workers;
        std::vector<pthread_t*> worker_threads;
        
        // How many threads are writing to disk at once?
        int nworkers = 2;

        for (int i=0; i<nworkers; i++) {
            pthread_t* thread = new pthread_t;
            //RpcWorker* worker = new RpcWorker(_ctx, ZMQ_DEALER);
            //workers.push_back(worker);
            //pthread_create(thread, NULL, std::bind(&RpcWorker::work, worker), NULL);
            rpc_worker_thread_context *context = new rpc_worker_thread_context;
            context->ctx = &_ctx;
            pthread_create(thread, NULL, rpc_worker_thread_main, context);
            worker_threads.push_back(thread);
        }

        // This shouldn't return
        //zmq::proxy(_frontend, _backend, nullptr);
        for (;;) {
            zmq::message_t client;
            zmq::message_t req;
            _frontend.recv(&client);
            _frontend.recv(&req);

            cout << "Received message from client" << endl;

            //_backend.send(&client);
            //_backend.send(&req);
        }

        // FIXME -- join threads?
        for (int i=0; i<nworkers; i++) {
            //delete workers[i];
            delete worker_threads[i];
        }
    }

private:
    zmq::context_t &_ctx;
    zmq::socket_t _frontend;
    zmq::socket_t _backend;
    string _port;
};

struct rpc_thread_contextX {
    //shared_ptr<ch_frb_io::intensity_network_stream> stream;
    // eg, "tcp://*:5555";
    string port;
};

static void *rpc_thread_main(void *opaque_arg) {
    rpc_thread_contextX *context = reinterpret_cast<rpc_thread_contextX *> (opaque_arg);
    //shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    string port = context->port;
    delete context;

    zmq::context_t ctx;
    RpcServer rpc(ctx, port);
    rpc.run();
    return NULL;
}

void rpc_server_start(string port) {
    cout << "Starting RPC server on " << port << endl;

    rpc_thread_contextX *context = new rpc_thread_contextX;
    //context->stream = stream;
    context->port = port;

    pthread_t* rpc_thread = new pthread_t;

    //int err = pthread_create(&rpc_thread, NULL, rpc_thread_main, context);
    int err = pthread_create(rpc_thread, NULL, rpc_thread_main, context);
    if (err)
        throw runtime_error(string("pthread_create() failed to create RPC thread: ") + strerror(errno));
    
}


#include <unistd.h>


int main() {

    //rpc_server_start("tcp://*:5555");
    rpc_server_start("tcp://127.0.0.1:5555");

    L1Ringbuf rb;

    int beam = 77;
    int nupfreq = 4;
    int nt_per = 16;
    int fpga_per = 400;

    assembled_chunk* ch;
    //ch = assembled_chunk::make(4, nupfreq, nt_per, fpga_per, 42);

    std::random_device rd;
    std::mt19937 rng(rd());
    rng.seed(42);
    std::uniform_int_distribution<> rando(0,1);

    for (int i=0; i<100; i++) {
        ch = new assembled_chunk(beam, nupfreq, nt_per, fpga_per, i);
        cout << "Pushing " << i << endl;
        rb.push(ch);

        cout << "Pushed " << i << endl;
        rb.print();
        cout << endl;

        // downstream thread consumes with a lag of 2...
        if (i >= 2) {
            // Randomly consume 0 to 2 chunks
            if (rando(rng)) {
                cout << "Downstream consumes a chunk" << endl;
                rb.pop();
            }
            if (rando(rng)) {
                cout << "Downstream consumes a chunk" << endl;
                rb.pop();
            }
        }
    }

    cout << "End state:" << endl;
    rb.print();
    cout << endl;


    vector<shared_ptr<assembled_chunk> > chunks;
    cout << "Retrieving chunks..." << endl;
    rb.retrieve(30000000, 50000000, chunks);
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

