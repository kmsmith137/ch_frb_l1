#include <iostream>
#include <sstream>
#include <pthread.h>
#include <unistd.h>
#include <string>

//  ZeroMQ experiment, from "Hello World server in C++"
//  Binds REP socket to tcp://*:5555
//
#include <zmq.hpp>

// msgpack
#include <msgpack.hpp>

#include <ch_frb_io.hpp>
#include <ch_frb_rpc.hpp>
#include <rpc.hpp>

using namespace std;

frb_rpc_server::frb_rpc_server(std::shared_ptr<intensity_network_stream> s) :
    stream(s)
{
}

frb_rpc_server::~frb_rpc_server() {}

struct rpc_thread_context {
    shared_ptr<ch_frb_io::intensity_network_stream> stream;
};

/**

 RPC calls:

 - [dict] get_beam_metadata(void)
 ---> returns an array of dictionaries? describing the beams handled by this
      L1 node.  Elements would include:
        int beam_id
        int nupfreq
        int nt_per_packet
        int fpga_counts_per_sample
        constants like nt_per_assembled_chunk, nfreq_coarse?
        int64 fpga_count // first fpga count seen; 0 if no L0 packets seen yet
        int ring buffer capacity?
        int ring buffer size?
        int64 min_fpga_count // in ring buffer
        int64 max_fpga_count // in ring buffer
        <packet count statistics>
        <packet counts from each L0 node>

 - [intensity_packet] get_packets([
          ( beam_id,
            // high index non-inclusive; 0 means max (no cut)
            fpga_count_low, fpga_count_high,
            freq_coarse_low, freq_coarse_high,
            upfreq_low, upfreq_high,
            tsamp_low, tsamp_high )
          ])
 ---> returns an array of intensity_packets (or sub-packets).

 - [bool] dump_packets(...)
 ---> to disk?

 */




static void *rpc_thread_main(void *opaque_arg) {
    rpc_thread_context *context = reinterpret_cast<rpc_thread_context *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    delete context;

    cout << "Hello, I am the RPC thread" << endl;

    // ZMQ
    //  Prepare our context and socket
    zmq::context_t zcontext(1);
    zmq::socket_t socket(zcontext, ZMQ_REP);
    //socket.bind("tcp://localhost:5555");
    socket.bind("tcp://*:5555");

    while (true) {
        zmq::message_t request;

        //  Wait for next request from client
        socket.recv(&request);
        std::cout << "Received RPC request" << std::endl;

        const char* req_data = reinterpret_cast<const char *>(request.data());
        std::size_t offset = 0;

        // Unpack the function name (string)
        msgpack::object_handle oh =
            msgpack::unpack(req_data, request.size(), offset);
        string funcname = oh.get().as<string>();
        cout << " Function name: " << funcname << endl;

        // RPC reply
        msgpack::sbuffer buffer;

        if (funcname == "get_beam_metadata") {
            cout << "get_beam_metadata() called" << endl;
            // No input arguments, so don't unpack anything more
            std::vector<
                std::unordered_map<std::string, uint64_t> > R =
                stream->get_statistics();
            msgpack::pack(buffer, R);
        } else if (funcname == "get_chunks") {
            cout << "get_chunks() called" << endl;

            // grab arg
            cout << "Grabbing arguments" << endl;
            msgpack::object_handle oh =
                msgpack::unpack(req_data, request.size(), offset);
            cout << "Beams: " << oh.get() << endl;
            vector<uint64_t> beams = oh.get().as<vector<uint64_t> >();

            uint64_t min_chunk, max_chunk;
            msgpack::unpack(req_data, request.size(), offset).get().convert(&min_chunk);
            msgpack::unpack(req_data, request.size(), offset).get().convert(&max_chunk);
            cout << "Min/max chunk: " << min_chunk << ", " << max_chunk << endl;

            vector<vector<shared_ptr<assembled_chunk> > > snaps = stream->get_ringbuf_snapshots(beams);

            for (auto it = snaps.begin(); it != snaps.end(); it++) {
                cout << "Beam chunks:" << endl;
                for (vector<shared_ptr<assembled_chunk> >::iterator it2 = it->begin(); it2 != it->end(); *it2++) {
                    cout << "  chunk: " << it2->get() << endl;
                }
            }

            msgpack::pack(buffer, snaps);


        } else if (funcname == "get_chunks_2") {
            cout << "get_chunks_2() called" << endl;

            // grab arg
            cout << "Grabbing arguments" << endl;
            msgpack::object_handle oh =
                msgpack::unpack(req_data, request.size(), offset);
            cout << "Beams: " << oh.get() << endl;
            GetChunks_Request req = oh.get().as<GetChunks_Request>();

            vector<vector<shared_ptr<assembled_chunk> > > snaps = stream->get_ringbuf_snapshots(req.beams);

            for (auto it = snaps.begin(); it != snaps.end(); it++) {
                cout << "Beam chunks:" << endl;
                for (vector<shared_ptr<assembled_chunk> >::iterator it2 = it->begin(); it2 != it->end(); *it2++) {
                    cout << "  chunk: " << it2->get() << endl;
                }
            }

            msgpack::pack(buffer, snaps);



        } else {
            msgpack::pack(buffer, "No such RPC method");
        }

        //  Send reply back to client
        cout << "Sending RPC reply of size " << buffer.size() << endl;
        zmq::message_t reply(buffer.data(), buffer.size(), NULL);
        socket.send(reply);
        cout << "Sent" << endl;
    }

    return NULL;
}

void frb_rpc_server::start() {
    cout << "Starting RPC server..." << endl;

    rpc_thread_context *context = new rpc_thread_context;
    context->stream = stream;

    int err = pthread_create(&rpc_thread, NULL, rpc_thread_main, context);
    if (err)
        throw runtime_error(string("pthread_create() failed to create RPC thread: ") + strerror(errno));
    
}

void frb_rpc_server::stop() {
    cout << "Stopping RPC server..." << endl;
}


