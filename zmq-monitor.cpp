#include <string>
#include <zmq.hpp>
#include <thread>

#include "chlog.hpp"

using namespace std;

void _monitor_socket(zmq::context_t* ctx, string mon_name, string name) {
    // register a monitor endpoint for all socket events
    zmq_event_t event;
    zmq::socket_t s(*ctx, ZMQ_PAIR);
    chlog("ZMQ socket monitoring beginning for " << name);
    s.connect(mon_name.c_str());
    while (true) {
        zmq::message_t msg;
        zmq::message_t msg2;
        bool ok;
        ok = s.recv(&msg);
        if (!ok) {
            chlog("Failed to recv on socket monitor for " << name << ": " << strerror(errno));
            return;
        }
	int more = s.getsockopt<int>(ZMQ_RCVMORE);
        if (!more) {
            chlog("Monitoring socket failed for " << name << ": expected two message parts");
            break;
        }
        ok = s.recv(&msg2);
        if (!ok) {
            chlog("Failed to recv on socket monitor for " << name << ": " << strerror(errno));
            return;
        }
	more = s.getsockopt<int>(ZMQ_RCVMORE);
        if (more) {
            chlog("Monitoring socket failed for " << name << ": expected only two message parts");
            break;
        }
        string sock(static_cast<const char*>(msg2.data()), msg2.size());

        memset(&event, 0, sizeof(event));
        memcpy(&event, msg.data(), std::min(sizeof(event), msg.size()));
        switch (event.event) {
            // 1 (0x1)
        case ZMQ_EVENT_CONNECTED:
            chlog("ZMQ_EVENT_CONNECTED       on " << name << ", re " << sock);
            break;
            // 2 (0x2)
        case ZMQ_EVENT_CONNECT_DELAYED:
            chlog("ZMQ_EVENT_CONNECT_DELAYED on " << name << ", re " << sock);
            break;
            // 4 (0x4)
        case ZMQ_EVENT_CONNECT_RETRIED:
            chlog("ZMQ_EVENT_CONNECT_RETRIED on " << name << ", re " << sock);
            break;
            // 8 (0x8)
        case ZMQ_EVENT_LISTENING:
            chlog("ZMQ_EVENT_LISTENING       on " << name << ", re " << sock);
            break;
            // 16 (0x10)
        case ZMQ_EVENT_BIND_FAILED:
            chlog("ZMQ_EVENT_BIND_FAILED     on " << name << ", re " << sock);
            break;
            // 32 (0x20)
        case ZMQ_EVENT_ACCEPTED:
            chlog("ZMQ_EVENT_ACCEPTED        on " << name << ", re " << sock);
            break;
            // 128 (0x80)
        case ZMQ_EVENT_CLOSED:
            chlog("ZMQ_EVENT_CLOSED          on " << name << ", re " << sock);
            break;
            // 512 (0x200)
        case ZMQ_EVENT_DISCONNECTED:
            chlog("ZMQ_EVENT_DISCONNECTED    on " << name << ", re " << sock);
            break;
        default:
            chlog("ZMQ socket event: " << int(event.event) << " on " << name << ", re " << sock);
            break;
        }
    }
    chlog("ZMQ socket monitoring ended for " << name);
}

void monitor_zmq_socket(zmq::context_t* ctx, zmq::socket_t& socket, string name) {
    string monitor_name = "inproc://monitor." + name;
    int rtn = zmq_socket_monitor((void*)(socket), monitor_name.c_str(), ZMQ_EVENT_ALL);
    if (rtn) {
        chlog("Failed to zmq_socket_monitor: " << strerror(errno));
        return;
    }
    std::thread t(std::bind(_monitor_socket, ctx, monitor_name, name));
    t.detach();
}

