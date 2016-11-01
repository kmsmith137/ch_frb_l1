#include <iostream>
#include <sstream>
#include <pthread.h>
#include <unistd.h>
#include <string>

#include <zmq.hpp>
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
    // eg, "tcp://*:5555";
    string port;
};





static void *rpc_thread_main(void *opaque_arg) {
    rpc_thread_context *context = reinterpret_cast<rpc_thread_context *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    string port = context->port;
    delete context;

    cout << "RPC thread running for port " << port << endl;

    // ZMQ
    //  Prepare our context and socket
    zmq::context_t zcontext(1);
    zmq::socket_t socket(zcontext, ZMQ_REP);
    socket.bind(port);

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

        // RPC reply
        msgpack::sbuffer buffer;

        if (funcname == "get_beam_metadata") {
            cout << "RPC get_beam_metadata() called" << endl;
            // No input arguments, so don't unpack anything more
            std::vector<
                std::unordered_map<std::string, uint64_t> > R =
                stream->get_statistics();
            msgpack::pack(buffer, R);

        } else if (funcname == "get_chunks") {
            cout << "RPC get_chunks() called" << endl;

            // grab GetChunks_Request argument
            msgpack::object_handle oh =
                msgpack::unpack(req_data, request.size(), offset);
            GetChunks_Request req = oh.get().as<GetChunks_Request>();

            vector<shared_ptr<assembled_chunk> > chunks;

            {
                // local block to limit the amount of time we hold these in scope
                // (because we might be preventing the memory from being reclaimed)
                vector<vector<shared_ptr<assembled_chunk> > > snaps = stream->get_ringbuf_snapshots(req.beams);
                for (auto it = snaps.begin(); it != snaps.end(); it++) {
                    for (vector<shared_ptr<assembled_chunk> >::iterator it2 = it->begin(); it2 != it->end(); *it2++) {
                        //cout << "  chunk: " << it2->get() << endl;
                        if (req.min_chunk && ((*it2)->ichunk < req.min_chunk))
                            continue;
                        if (req.max_chunk && ((*it2)->ichunk > req.max_chunk))
                            continue;
                        chunks.push_back(*it2);
                    }
                }
            }
            msgpack::pack(buffer, chunks);

        } else {
            cout << "Error: unknown RPC function name: " << funcname << endl;
            msgpack::pack(buffer, "No such RPC method");
        }

        //  Send reply back to client
        cout << "Sending RPC reply of size " << buffer.size() << endl;
        // FIXME -- this copies the buffer
        zmq::message_t reply(buffer.data(), buffer.size());
        int nsent = socket.send(reply);
        //cout << "Sent " << nsent << " (vs " << buffer.size() << ")" << endl;
        if (nsent == -1) {
            cout << "ERROR: sending RPC reply: "
                 << strerror(zmq_errno()) << endl;
        }
    }

    return NULL;
}

void frb_rpc_server::start(string port) {
    cout << "Starting RPC server on " << port << endl;

    rpc_thread_context *context = new rpc_thread_context;
    context->stream = stream;
    context->port = port;

    int err = pthread_create(&rpc_thread, NULL, rpc_thread_main, context);
    if (err)
        throw runtime_error(string("pthread_create() failed to create RPC thread: ") + strerror(errno));
    
}

void frb_rpc_server::stop() {
    cout << "Stopping RPC server..." << endl;
}


