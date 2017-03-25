// This is a toy program which receives a packet stream and doesn't do anything with it!
// The sender will usually be the counterpart toy program 'ch-frb-l0-simulate'.
//
// This is a placeholder for the full L1 process.  Currently data goes through the network and
// assembler threads, but doesn't get RFI-cleaned or dedispersed.  This will be expanded soon!
// In the meantime, it's still useful for timing the network receive code.

#include <cassert>
#include <iostream>
#include <sstream>
#include <thread>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <ch_frb_io.hpp>
#include <l1-rpc.hpp>
#include <chlog.hpp>

using namespace std;
using namespace ch_frb_io;

// -------------------------------------------------------------------------------------------------
//
// Processing threads: we spawn one of these per beam, to process the real-time data.
// Currently this is a placeholder which reads the data and throws it away!

static void processing_thread_main(shared_ptr<ch_frb_io::intensity_network_stream> stream,
                                    int ithread) {
    chime_log_set_thread_name("proc-" + std::to_string(ithread));

    chlog("Processing thread main: thread " << ithread);

    const int nupfreq = 16;
    const int nalloc = ch_frb_io::constants::nfreq_coarse_tot * nupfreq * ch_frb_io::constants::nt_per_assembled_chunk;

    std::vector<float> intensity(nalloc, 0.0);
    std::vector<float> weights(nalloc, 0.0);

    for (;;) {
	// Get assembled data from netwrok
	auto chunk = stream->get_assembled_chunk(ithread);
	if (!chunk)
	    break;  // End-of-stream reached

	assert(chunk->nupfreq == nupfreq);

	// We call assembled_chunk::decode(), which extracts the data from its low-level 8-bit
	// representation to a floating-point array, but our processing currently stops there!
	chunk->decode(&intensity[0], &weights[0], ch_frb_io::constants::nt_per_assembled_chunk);
        chlog("Decoded beam " << chunk->beam_id << ", chunk " << chunk->ichunk);
    }
}

static void usage()
{
    cerr << "usage: ch-frb-l1 [-r]\n" <<
        "      [-u <L1 udp-port>]\n" <<
        "      [-a <RPC address>] [-p <RPC port number>]\n" <<
        "      [-b <beam id> (may be repeated)]\n" <<
        "      [-h for help]" <<
        "   -r uses reference kernels instead of avx2 kernels\n" <<
        "  Addresses are like:\n" <<
        "   -a tcp://127.0.0.1:5555" << endl;
    exit(2);
}


int main(int argc, char **argv) {

    chime_log_open_socket();
    chime_log_set_thread_name("main");

    ch_frb_io::intensity_network_stream::initializer ini_params;

#ifdef __AVX2__
    ini_params.mandate_fast_kernels = true;
#else
    cerr << "Warning: this machine does not appear to have the AVX2 instruction set, fast kernels will be disabled\n";
#endif

    string rpc_port = "";
    int rpc_portnum = 0;

    int c;
    while ((c = getopt(argc, argv, "a:p:b:u:wh")) != -1) {
        switch (c) {
        case 'a':
            rpc_port = string(optarg);
            break;

        case 'p':
            rpc_portnum = atoi(optarg);
            break;

        case 'b':
            {
                int beam = atoi(optarg);
                ini_params.beam_ids.push_back(beam);
                break;
            }

        case 'u':
            {
                int udpport = atoi(optarg);
                ini_params.udp_port = udpport;
                break;
            }

        case 'r':
            ini_params.mandate_fast_kernels = false;
            ini_params.mandate_reference_kernels = true;
            break;

        case 'h':
        case '?':
        default:
            usage();
            return 0;
        }
    }
    argc -= optind;
    argv += optind;

    if (ini_params.beam_ids.size() == 0) {
        ini_params.beam_ids = { 0, 1, 2, 3, 4, 5, 6, 7 };
    }

    // Make input stream object
    auto stream = ch_frb_io::intensity_network_stream::make(ini_params);

    // Spawn one processing thread per beam
    std::vector<std::thread> processing_threads;
    for (unsigned int ibeam = 0; ibeam < ini_params.beam_ids.size(); ibeam++)
        // Note: the processing thread gets 'ibeam', not the beam id,
        // because that is what get_assembled_chunk() takes
        processing_threads.push_back(std::thread(std::bind(processing_thread_main, stream, ibeam)));

    if ((rpc_port.length() == 0) && (rpc_portnum == 0))
        rpc_port = "tcp://127.0.0.1:5555";
    else if (rpc_portnum)
        rpc_port = "tcp://127.0.0.1:" + to_string(rpc_portnum);

    chlog("Starting RPC server on " << rpc_port);
    L1RpcServer rpc(stream, rpc_port);
    rpc.start();

    // Start listening for packets.
    stream->start_stream();

    chlog("Waiting for network stream to end...");

    // This will block until the stream ends, and the network and assembler threads exit.
    // (The way this works is that the sender sends a special "end-of-stream" packet, to
    // indicate there is no more data, which initiates the stream shutdown process on the
    // receive side.)
    stream->join_threads();

    chlog("Network stream ended");

    // Join processing threads
    for (int ibeam = 0; ibeam < ini_params.beam_ids.size(); ibeam++) {
        chlog("Joining thread " << ibeam);
        processing_threads[ibeam].join();
    }

    return 0;
}
