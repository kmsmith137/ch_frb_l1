#include <iostream>
#include <sstream>
#include <pthread.h>
#include <ch_frb_io.hpp>
#include <ch_frb_rpc.hpp>
#include <unistd.h>

using namespace std;
//namespace event_type = ch_frb_io::intensity_network_stream::event_type;

frb_rpc_server::frb_rpc_server(std::shared_ptr<intensity_network_stream> s) :
    stream(s)
{
}

frb_rpc_server::~frb_rpc_server() {}

struct rpc_thread_context {
    shared_ptr<ch_frb_io::intensity_network_stream> stream;
};

static void *rpc_thread_main(void *opaque_arg) {
    rpc_thread_context *context = reinterpret_cast<rpc_thread_context *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    delete context;

    cout << "Hello, I am the RPC thread" << endl;

    for (;;) {
        usleep(5000000);

        // stream->  std::shared_ptr<assembled_chunk> get_assembled_chunk(int assembler_index);
        std::vector<int64_t> counts = stream->get_event_counts();

        stringstream ss;
        ss << "ch_frb_rpc: stats:\n"
           << "    bytes received (GB): " << (1.0e-9 * counts[intensity_network_stream::event_type::byte_received]) << "\n"
           << "    packets received: " << counts[intensity_network_stream::event_type::packet_received] << "\n"
           << "    good packets: " << counts[intensity_network_stream::event_type::packet_good] << "\n"
           << "    bad packets: " << counts[intensity_network_stream::event_type::packet_bad] << "\n"
           << "    dropped packets: " << counts[intensity_network_stream::event_type::packet_dropped] << "\n"
           << "    end-of-stream packets: " << counts[intensity_network_stream::event_type::packet_end_of_stream] << "\n"
           << "    beam id mismatches: " << counts[intensity_network_stream::event_type::beam_id_mismatch] << "\n"
           << "    first-packet mismatches: " << counts[intensity_network_stream::event_type::first_packet_mismatch] << "\n"
           << "    assembler hits: " << counts[intensity_network_stream::event_type::assembler_hit] << "\n"
           << "    assembler misses: " << counts[intensity_network_stream::event_type::assembler_miss] << "\n"
           << "    assembled chunks dropped: " << counts[intensity_network_stream::event_type::assembled_chunk_dropped] << "\n"
           << "    assembled chunks queued: " << counts[intensity_network_stream::event_type::assembled_chunk_queued] << "\n";
        cout << ss.str().c_str();
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
//pthread_t network_thread;

