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

        //  Do some 'work'
        sleep(1);

        //  Send reply back to client
        zmq::message_t reply(5);
        memcpy(reply.data(), "World", 5);
        socket.send(reply);
    }

    /*
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
     */

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


