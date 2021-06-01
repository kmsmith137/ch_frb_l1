#include <string>
#include <zmq.hpp>

void monitor_zmq_socket(zmq::context_t* ctx, zmq::socket_t& socket, std::string name);

