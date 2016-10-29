/*
 * 
 */
#include <cassert>
#include <iostream>
#include <sstream>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ch_frb_io.hpp>
#include <ch_frb_rpc.hpp>

#if defined(__AVX2__)
const static bool HAVE_AVX2 = true;
#else
#warning "This machine does not have the AVX2 instruction set."
const static bool HAVE_AVX2 = false;
#endif

using namespace std;

struct processing_thread_context {
    shared_ptr<ch_frb_io::intensity_network_stream> stream;
    int ithread = -1;
};

static void *processing_thread_main(void *opaque_arg)
{
    const int nupfreq = 16;
    const int nalloc = ch_frb_io::constants::nfreq_coarse_tot * nupfreq * ch_frb_io::constants::nt_per_assembled_chunk;

    std::vector<float> intensity(nalloc, 0.0);
    std::vector<float> weights(nalloc, 0.0);

    processing_thread_context *context = reinterpret_cast<processing_thread_context *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    int ithread = context->ithread;
    delete context;

    for (;;) {
	// Get assembled data from netwrok
	auto chunk = stream->get_assembled_chunk(ithread);
	if (!chunk)
	    break;  // End-of-stream reached

	assert(chunk->nupfreq == nupfreq);

	// We call assembled_chunk::decode(), which extracts the data from its low-level 8-bit
	// representation to a floating-point array, but our processing currently stops there!
	chunk->decode(&intensity[0], &weights[0], ch_frb_io::constants::nt_per_assembled_chunk);
    cout << "Decoded beam " << chunk->beam_id << ", chunk " << chunk->ichunk << endl;
    }
    return NULL;
}

static void spawn_processing_thread(pthread_t &thread, const shared_ptr<ch_frb_io::intensity_network_stream> &stream, int ithread)
{
    processing_thread_context *context = new processing_thread_context;
    context->stream = stream;
    context->ithread = ithread;

    int err = pthread_create(&thread, NULL, processing_thread_main, context);
    if (err)
	throw runtime_error(string("pthread_create() failed to create processing thread: ") + strerror(errno));

    // no "delete context"!
}

/*
 * 
 */
int main(int argc, char** argv) {

    const int n_l1_nodes = 4;
    const int n_beams_per_l1_node = 4;
    const int n_l0_nodes = 8;
    
    const int udp_port_l1_base = 10255;
    const int rpc_port_l1_base = 5555;

    vector<shared_ptr<intensity_network_stream> > streams;
    // Spawn one processing thread per beam
    pthread_t processing_threads[n_l1_nodes * n_beams_per_l1_node];

    vector<shared_ptr<frb_rpc_server> > rpcs;

    for (int i=0; i<n_l1_nodes; i++) {
        ch_frb_io::intensity_network_stream::initializer ini_params;
        for (int j=0; j<n_beams_per_l1_node; j++) {
            ini_params.beam_ids.push_back(i*n_beams_per_l1_node + j);
        }
        ini_params.mandate_fast_kernels = HAVE_AVX2;
        ini_params.udp_port = udp_port_l1_base + i;

        shared_ptr<intensity_network_stream> stream = 
            ch_frb_io::intensity_network_stream::make(ini_params);
        streams.push_back(stream);

        // Spawn one processing thread per beam
        for (int j=0; j<n_beams_per_l1_node; j++) {
            int ibeam = i*n_beams_per_l1_node + j;
            spawn_processing_thread(processing_threads[ibeam], stream, j);
        }

        // Make RPC-serving object for each L1 node.
        stringstream ss;
        ss << "tcp://127.0.0.1:" << (rpc_port_l1_base + i);
        string port = ss.str();
        // string port = "tcp://localhost:" + string(rpc_port_l1_base + i);
        cout << "Starting RPC server on " << port << endl;
        shared_ptr<frb_rpc_server> rpc(new frb_rpc_server(stream));
        rpc->start(port);
        rpcs.push_back(rpc);

        usleep(100000);
    }

    for (int i=0; i<n_l0_nodes; i++) {
        // Create sim L0 node...
    }
    
    return 0;
}

