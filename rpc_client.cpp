//  ZeroMQ experiment, from "Hello World client in C++"
//  Connects REQ socket to tcp://localhost:5555
//  Sends "Hello" to server, expects "World" back
#include <zmq.hpp>
#include <string>
#include <iostream>

// msgpack
#include <msgpack.hpp>
#include <sstream>

using namespace std;

int main() {
    //  Prepare our context and socket
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REQ);
    
    std::cout << "Connecting to hello world server…" << std::endl;
    socket.connect("tcp://localhost:5555");
    
    //  Do 10 requests, waiting each time for a response
    for (int request_nbr = 0; request_nbr < 10; request_nbr++) {

        // RPC request buffer.
        msgpack::sbuffer buffer;
        std::string funcname = "get_beam_metadata";
        msgpack::pack(buffer, funcname);

        cout << "Buffer size: " << buffer.size() << ", data: " << buffer.data() << endl;

        // no copy
        zmq::message_t request(buffer.data(), buffer.size(), NULL);

        std::cout << "Sending Hello " << request_nbr << std::endl;
        socket.send(request);

        //  Get the reply.
        zmq::message_t reply;
        socket.recv(&reply);
        std::cout << "Received result " << request_nbr << std::endl;

        cout << "Reply has size " << reply.size() << " and data: " << reply.data() << endl;

        const char* reply_data = reinterpret_cast<const char *>(reply.data());

        msgpack::object_handle oh =
            msgpack::unpack(reply_data, reply.size());
        msgpack::object obj = oh.get();
        std::cout << obj << std::endl;

    }
    return 0;
}
