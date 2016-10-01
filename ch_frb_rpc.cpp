#include <iostream>
#include <sstream>
#include <pthread.h>
#include <ch_frb_io.hpp>
#include <ch_frb_rpc.hpp>
#include <unistd.h>

//  ZeroMQ experiment, from "Hello World server in C++"
//  Binds REP socket to tcp://*:5555
//
#include <zmq.hpp>
#include <string>

// msgpack
#include <msgpack.hpp>

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
    socket.bind("tcp://*:5555");

    while (true) {
        zmq::message_t request;

        //  Wait for next request from client
        socket.recv(&request);
        std::cout << "Received RPC request" << std::endl;

        const char* req_data = reinterpret_cast<const char *>(request.data());

        std::size_t offset = 0;
        //msgpack::unpacker pac;
        // feed the buffer.
        //pac.reserve_buffer(request.size());
        //memcpy(pac.buffer(), buffer.data(), buffer.size());
        //pac.buffer_consumed(buffer.size());

        msgpack::object_handle oh =
            msgpack::unpack(req_data, request.size(), offset);

        //msgpack::object obj = oh.get();
        //string funcname = obj[0];
        //msgpack::object args = obj[1];
        string funcname = oh.get().as<string>();

        //cout << "Request object: " << obj << endl;
        cout << " Function name: " << funcname << endl;
        //cout << " Function args: " << args << endl;

        // RPC reply
        msgpack::sbuffer buffer;

        if (funcname == "get_beam_metadata") {
            cout << "get_beam_metadata() called" << endl;
            std::vector<
                std::unordered_map<std::string, uint64_t> > R =
                stream->get_statistics();
            msgpack::pack(buffer, R);
        } else if (funcname == "get_chunks") {
            cout << "get_chunks() called" << endl;

            // grab arg
            cout << "Grabbing argument... offset: " << offset << endl;
            msgpack::object_handle oh =
                msgpack::unpack(req_data, request.size(), offset);
            std::vector<std::vector<uint64_t> > args;
            cout << "Arg: " << oh.get() << endl;
            args = oh.get().as<decltype(args)>();

            cout << "Chunks: " << args.size() << endl;
            for (int i=0; i<args.size(); i++) {
                std::vector<uint64_t> chunk = args[i];
                cout << "Chunk: " << chunk[0] << ", " << chunk[1] << endl;
            }

        } else {
            msgpack::pack(buffer, "No such RPC method");
        }

        //  Send reply back to client
        cout << "Sending RPC reply of size " << buffer.size() << endl;
        zmq::message_t reply(buffer.data(), buffer.size(), NULL);
        socket.send(reply);
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


