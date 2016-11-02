//  ZeroMQ experiment, from "Hello World client in C++"
//  Connects REQ socket to tcp://localhost:5555
//  Sends "Hello" to server, expects "World" back
#include <zmq.hpp>
#include <string>
#include <iostream>

// msgpack
#include <msgpack.hpp>
#include <sstream>

#include <rpc.hpp>

using namespace std;

int main() {
    //  Prepare our context and socket
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REQ);
    
    cout << "Connecting to hello world server…" << endl;
    socket.connect("tcp://localhost:5555");
    
    //  Do 10 requests, waiting each time for a response
    for (int request_nbr = 0; request_nbr < 10; request_nbr++) {

        // RPC request buffer.
        msgpack::sbuffer buffer;
        string funcname = "get_beam_metadata";
        //tuple<string, bool> call(funcname, false);
        //msgpack::pack(buffer, call);
        msgpack::pack(buffer, funcname);

        cout << "Buffer size: " << buffer.size() << endl;

        // no copy
        zmq::message_t request(buffer.data(), buffer.size(), NULL);

        cout << "Sending metadata request " << request_nbr << endl;
        socket.send(request);

        //  Get the reply.
        zmq::message_t reply;
        socket.recv(&reply);
        cout << "Received result " << request_nbr << endl;

        cout << "Reply has size " << reply.size() << endl;

        const char* reply_data = reinterpret_cast<const char *>(reply.data());

        msgpack::object_handle oh =
            msgpack::unpack(reply_data, reply.size());
        msgpack::object obj = oh.get();
        cout << obj << endl;


        // Send chunk request
        buffer = msgpack::sbuffer();
        
        funcname = "get_chunks";
        msgpack::pack(buffer, funcname);

        GetChunks_Request req;
        req.beams.push_back(2);
        req.min_chunk = 1;
        req.max_chunk = 1000;
        msgpack::pack(buffer, req);

        cout << "Buffer size: " << buffer.size() << endl;
        // no copy
        request = zmq::message_t(buffer.data(), buffer.size(), NULL);
        cout << "Sending chunk request " << request_nbr << endl;
        socket.send(request);

        //  Get the reply.
        socket.recv(&reply);
        cout << "Received result " << endl;
        cout << "Reply has size " << reply.size() << endl;
        reply_data = reinterpret_cast<const char *>(reply.data());
        oh = msgpack::unpack(reply_data, reply.size());
        obj = oh.get();

        vector<vector<shared_ptr<assembled_chunk> > > beamchunks2;
        obj.convert(&beamchunks2);

        cout << "Beam-chunks: " << beamchunks2.size() << endl;
        for (auto it = beamchunks2.begin(); it != beamchunks2.end(); it++) {
            cout << "Beam chunks:" << endl;
            for (auto it2 = it->begin(); it2 != it->end(); *it2++) {
                cout << "  chunk: " << (*it2)->ndata << " data" << endl;
            }
        }

        break;

    }
    return 0;
}
