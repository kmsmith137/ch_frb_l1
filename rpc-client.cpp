// Toy C++ client which sends get_statistics RPC, and prints the result
// (as a raw msgpack object without decoding, but it's vaguely human-readable)

#include <zmq.hpp>
#include <string>
#include <iostream>

// msgpack
#include <msgpack.hpp>
#include <sstream>

#include <rpc.hpp>

using namespace std;

int main(int argc, char **argv) 
{
    if (argc != 2) {
	cerr << "usage: rpc-client <destination>\n"
	     << "   where <destination> is a string such as tcp://10.0.0.101:5555\n"
	     << "\n"
	     << "This toy C++ client sends a get_statistics RPC, and prints ther result\n"
	     << "(as a raw msgpack object without decoding, but it's vaguely human-readable)\n";

	exit(2);
    }
    
    const char *destination = argv[1];

    //  Prepare our context and socket
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_DEALER);
    
    cout << "Connecting to L1 RPC server at..." << destination << endl;
    socket.connect(destination);
    
    //  Do 10 requests, waiting each time for a response
    for (int request_nbr = 0; request_nbr < 1; request_nbr++) {

        // RPC request buffer.
        msgpack::sbuffer buffer;
        Rpc_Request rpc;
        rpc.function = "get_statistics";
        rpc.token = 42;

        msgpack::pack(buffer, rpc);
        cout << "Buffer size: " << buffer.size() << endl;

        // no copy
        zmq::message_t request(buffer.data(), buffer.size(), NULL);

        cout << "Sending stats request " << request_nbr << endl;
        socket.send(request);

        //  Get the reply: token followed by data
        zmq::message_t reply_token;
        zmq::message_t reply;
        socket.recv(&reply_token);
        socket.recv(&reply);
        cout << "Received result " << request_nbr << endl;

        const char* token_data = reinterpret_cast<const char *>(reply_token.data());
        msgpack::object_handle toh = msgpack::unpack(token_data, reply_token.size());
        uint32_t token = toh.get().as<uint32_t>();
        cout << "Token: " << token << endl;

        cout << "Reply has size " << reply.size() << endl;
        const char* reply_data = reinterpret_cast<const char *>(reply.data());
        msgpack::object_handle oh = msgpack::unpack(reply_data, reply.size());
        msgpack::object obj = oh.get();
        cout << obj << endl;

        /*
        // Send chunk request
        buffer = msgpack::sbuffer();
        
        funcname = "get_chunks";
        msgpack::pack(buffer, funcname);

        GetChunks_Request req;
        req.beams.push_back(2);
        req.min_fpga = 0;
        req.max_fpga = 1000*400*1000;
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
         */
    }

    return 0;
}
