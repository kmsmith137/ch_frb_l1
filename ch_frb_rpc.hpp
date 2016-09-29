#include <pthread.h>
#include <ch_frb_io.hpp>

using namespace ch_frb_io;

class frb_rpc_server {

public:
    frb_rpc_server(std::shared_ptr<intensity_network_stream>);
    ~frb_rpc_server();

    void start();
    void stop();

protected:
    pthread_t rpc_thread;
    std::shared_ptr<intensity_network_stream> stream;

};


