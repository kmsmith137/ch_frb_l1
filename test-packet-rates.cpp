#include <unistd.h>
#include <iostream>
#include <sstream>
#include <ch_frb_io.hpp>
#include <l1-rpc.hpp>
#include <chlog.hpp>
#include <arpa/inet.h>

using namespace std;
using namespace ch_frb_io;

int main(int argc, char** argv) {
    int nstreams = 32;
    int nbeams = 1;
    int nsenders = 256;
    
    int portnum = 5555;
    int udpport = 6677;

    float drop_rate = 0.01;
    
    int c;
    while ((c = getopt(argc, argv, "a:p:b:u:ws:n:h")) != -1) {
        switch (c) {
        case 'n':
            nstreams = atoi(optarg);
            break;
        case 's':
            nsenders = atoi(optarg);
            break;
        case 'p':
            portnum = atoi(optarg);
            break;
        case 'u':
            udpport = atoi(optarg);
            break;
        case 'd':
            drop_rate = atof(optarg);
            break;
        case 'h':
        case '?':
        default:
            cout << string(argv[0]) << ": [-n <N streams/NICs/RPC endpoints, default 4>] [-p <RPC port number start>] [-u <L1 udp-port start>] [-s <N> to fake receiving packets from N senders (default 256)] [-h for help]" << endl;
            cout << "eg,  -a tcp://127.0.0.1:5555" << endl;
            cout << "     -p 5555" << endl;
            cout << "     -b 78" << endl;
            return 0;
        }
    }
    argc -= optind;
    argv += optind;

    chime_log_open_socket();
    chime_log_set_thread_name("main");

    int nupfreq = 4;
    int nt_per = 16;
    int fpga_per = 400;

    output_device::initializer out_params;
    out_params.device_name = "";
    // debug
    out_params.verbosity = 3;
    std::shared_ptr<output_device> outdev = output_device::make(out_params);

    std::vector<std::shared_ptr<intensity_network_stream> > streams;
    std::vector<std::shared_ptr<L1RpcServer> > rpcs;
    std::vector<std::thread> rpc_threads(nstreams);

    FILE* fout = fopen("test-packet-rates.yaml", "w");
    fprintf(fout, "rpc_address: [ ");

    for (int i=0; i<nstreams; i++) {
        intensity_network_stream::initializer ini;
        for (int b=0; b<nbeams; b++)
            ini.beam_ids.push_back(1 + (i*nbeams) + b);
        ini.nupfreq = nupfreq;
        ini.nt_per_packet = nt_per;
        ini.fpga_counts_per_sample = fpga_per;
        //ini.force_fast_kernels = HAVE_AVX2;

        ini.telescoping_ringbuf_capacity.push_back(4);
        ini.telescoping_ringbuf_capacity.push_back(4);
        ini.telescoping_ringbuf_capacity.push_back(4);
        ini.telescoping_ringbuf_capacity.push_back(4);
    
        ini.udp_port = udpport + i;
        ini.output_devices.push_back(outdev);

        shared_ptr<intensity_network_stream> stream = intensity_network_stream::make(ini);
        stream->start_stream();

        streams.push_back(stream);
        
        string rpc_addr = "tcp://127.0.0.1:" + to_string(portnum + i);
        shared_ptr<L1RpcServer> rpc = make_shared<L1RpcServer>(stream, rpc_addr);
        rpcs.push_back(rpc);
        rpc_threads[i] = rpc->start();

        fprintf(fout, "%s\"%s\"", (i>0 ? ", " : ""), rpc_addr.c_str());
        
        chlog("Starting RPC server on port " << rpc_addr);
    }

    fprintf(fout, " ]\n");
    fclose(fout);
    
    std::random_device rd;
    std::mt19937 rng(rd());
    rng.seed(42);
    std::uniform_real_distribution<> rando(0.0, 1.0);

    std::vector<struct sockaddr_in> senders(nsenders);
    for (int i=0; i<nsenders; i++) {
        string sender = "10.0." + to_string(i / 16) + "." + to_string(i % 16) + ":8888";
        inet_aton(sender.c_str(), &senders[i].sin_addr);
    }

    // If a NIC receives 4 beams x 16k frequencies x 1k samples/sec * 8 bit = 512 Gbitps
    // With 256 L0 nodes, each is doing 16k/256 = 64 frequencies
    // Each L0 node processes all 1024 beams for its set of frequencies
    // Each packet has 4 beams x 64 frequencies x 16 samples = 4kbytes
    // Each L0 node has to send 64 packets / second to each L1 node (1k samples/sec / 16 samples/packet)

    int packet_nbytes = 4096;
    float sleep_usec = 1e6 / float(64 * 256 * nstreams); // = 61 microseconds / nstreams

    // how much of the time do we spend computing?
    sleep_usec *= 0.4;

    float tosleep = 0.0;

    int ndropped = 0;
    int nsent = 0;

    for (int k=0;; k++) {

        for (int i=0; i<nstreams; i++) {
            for (int s=0; s<nsenders; s++) {
                // Drop this packet?
                if (rando(rng) <= drop_rate) {
                    ndropped++;
                } else {
                    streams[i]->fake_packet_from(senders[s], packet_nbytes);
                    nsent++;
                }
                tosleep += sleep_usec * 2.0 * rando(rng);
                if (tosleep > 1.) {
                    int isleep = (int)tosleep;
                    tosleep -= isleep;
                    usleep(isleep);
                }
            }
        }
        if (k && (k % 64 == 0)) {
            cout << "Sent " << nsent << ", dropped " << ndropped << endl;
            nsent = ndropped = 0;
        }
    }
    chlog("Exiting");

    //rpc.do_shutdown();
    //rpc_thread.join();
}
