#include <unistd.h>
#include <iostream>
#include <sstream>
#include <ch_frb_io.hpp>
#include <l1-rpc.hpp>
#include <chlog.hpp>

using namespace std;
using namespace ch_frb_io;

#include "CivetServer.h"

int metric_counter = 0;

uint64_t last_network_thread_waiting = 0;
uint64_t last_network_thread_working = 0;
uint64_t last_assembler_thread_waiting = 0;
uint64_t last_assembler_thread_working = 0;

class L1PrometheusHandler : public CivetHandler {
public:
    L1PrometheusHandler(shared_ptr<intensity_network_stream> stream) :
        CivetHandler(),
        _stream(stream) {}

    bool handleGet(CivetServer *server, struct mg_connection *conn) {
        //cout << "test-l1-rpc: serving metrics" << endl;
        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/plain\r\n"
                  "Connection: close\r\n\r\n");

        metric_counter++;
        const char* name = "chime_frb_l1_metrics_count";
        mg_printf(conn,
                  "# HELP %s Number of times Prometheus metrics have been polled\n"
                  "# TYPE %s gauge\n"
                  "%s %i\n", name, name, name, metric_counter);

        vector<unordered_map<string, uint64_t> > stats = _stream->get_statistics();

        // Stats for the whole stream.
        unordered_map<string, uint64_t> sstats = stats[0];
        
        uint64_t wait = sstats["network_thread_waiting_usec"];
        uint64_t work = sstats["network_thread_working_usec"];

        uint64_t dwait = wait - last_network_thread_waiting;
        uint64_t dwork = work - last_network_thread_working;
        float fwork;
        fwork = (dwait+dwork == 0) ? 0. : (dwork / (dwait + dwork));

        last_network_thread_waiting = wait;
        last_network_thread_working = work;
        
        name = "network_thread_fraction";
        mg_printf(conn,
                  "# HELP %s Instantaneous fraction of the time the network thread is working\n"
                  "# TYPE %s gauge\n"
                  "%s %f\n", name, name, name, fwork);

        wait = sstats["assembler_thread_waiting_usec"];
        work = sstats["assembler_thread_working_usec"];

        dwait = wait - last_assembler_thread_waiting;
        dwork = work - last_assembler_thread_working;
        fwork = (dwait+dwork == 0) ? 0. : (dwork / (dwait + dwork));

        last_assembler_thread_waiting = wait;
        last_assembler_thread_working = work;
        
        name = "assembler_thread_fraction";
        mg_printf(conn,
                  "# HELP %s Instantaneous fraction of the time the assembler thread is working\n"
                  "# TYPE %s gauge\n"
                  "%s %f\n", name, name, name, fwork);

        // Stats per beam.  It seems that prometheus array values for
        // a single variable type have to appear together, hence the
        // looping over beams here
        name = "ringbuf_fpga_min";
        mg_printf(conn,
                  "# HELP %s Smallest FPGA-counts timestamp in the ring buffer, per beam\n"
                  "# TYPE %s gauge\n", name, name);
        for (size_t i=2; i<stats.size(); i++) {
            unordered_map<string, uint64_t> bstats = stats[i];
            mg_printf(conn, "%s{beam=\"%i\"} %lu\n", name, (int)bstats["beam_id"], (long)bstats["ringbuf_fpga_min"]);
        }

        name = "ringbuf_fpga_max";
        mg_printf(conn,
                  "# HELP %s Largest FPGA-counts timestamp in the ring buffer, per beam\n"
                  "# TYPE %s gauge\n", name, name);
        for (size_t i=2; i<stats.size(); i++) {
            unordered_map<string, uint64_t> bstats = stats[i];
            mg_printf(conn, "%s{beam=\"%i\"} %lu\n", name, (int)bstats["beam_id"], (long)bstats["ringbuf_fpga_max"]);
        }

        // Ring buffer stats per beam AND downsampling level.
        vector<vector<pair<shared_ptr<assembled_chunk>,uint64_t> > > snap = _stream->get_ringbuf_snapshots();
        // summary: per-beam x per-level min and max FPGA counts and N chunks
        map<pair<int, uint64_t>, uint64_t> minfpgas;
        map<pair<int, uint64_t>, uint64_t> maxfpgas;
        map<pair<int, uint64_t>, uint64_t> nchunks;
        // per beam
        for (auto it=snap.begin(); it!=snap.end(); it++) {
            // per (chunk,where)
            for (auto chw=it->begin(); chw!=it->end(); chw++) {
                pair<int, uint64_t> key = make_pair(chw->first->beam_id, chw->second);
                auto v = minfpgas.find(key);
                if (v == minfpgas.end()) {
                    // new
                    minfpgas[key] = chw->first->fpga_begin;
                    maxfpgas[key] = chw->first->fpga_end;
                    nchunks [key] = 1;
                } else {
                    minfpgas[key] = min(minfpgas[key], chw->first->fpga_begin);
                    maxfpgas[key] = max(maxfpgas[key], chw->first->fpga_end);
                    nchunks [key] = nchunks[key] + 1;
                }
            }
        }
        // Drop the snapshot (releasing assembled_chunks)
        snap.clear();

        name = "ringbuf_level_fpga_min";
        mg_printf(conn,
                  "# HELP %s Smallest FPGA-counts timestamp in the ring buffer, per beam AND binning\n"
                  "# TYPE %s gauge\n", name, name);
        for (auto it=minfpgas.begin(); it!=minfpgas.end(); it++) {
            auto key = it->first;
            mg_printf(conn, "%s{beam=\"%i\",level=\"%i\"} %lu\n", name, key.first, (int)key.second, (long)it->second);
        }
        name = "ringbuf_level_fpga_max";
        mg_printf(conn,
                  "# HELP %s Largest FPGA-counts timestamp in the ring buffer, per beam AND binning\n"
                  "# TYPE %s gauge\n", name, name);
        for (auto it=maxfpgas.begin(); it!=maxfpgas.end(); it++) {
            auto key = it->first;
            mg_printf(conn, "%s{beam=\"%i\",level=\"%i\"} %lu\n", name, key.first, (int)key.second, (long)it->second);
        }
        name = "ringbuf_level_nchunks";
        mg_printf(conn,
                  "# HELP %s Number of chunks in the ring buffer, per beam AND binning\n"
                  "# TYPE %s gauge\n", name, name);
        for (auto it=nchunks.begin(); it!=nchunks.end(); it++) {
            auto key = it->first;
            mg_printf(conn, "%s{beam=\"%i\",level=\"%i\"} %lu\n", name, key.first, (int)key.second, (long)it->second);
        }
        
        return true;
    }

protected:
    shared_ptr<intensity_network_stream> _stream;
};

shared_ptr<CivetServer> start_web_server(int port,
                                         shared_ptr<intensity_network_stream> stream) {
    //"document_root", DOCUMENT_ROOT, "listening_ports", PORT, 0};
    std::vector<std::string> options;
    options.push_back("listening_ports");
    options.push_back(to_string(port));
    shared_ptr<CivetServer> server;
    try {
        server = make_shared<CivetServer>(options);
    } catch (CivetException &e) {
        cout << "Failed to start web server on port " << port << ": "
             << e.what() << endl;
        return server;
    }
    // we're going to memory-leak this handler object
    L1PrometheusHandler* h = new L1PrometheusHandler(stream);
    server->addHandler("/metrics", h);
    return server;
}

int main(int argc, char** argv) {
    string port = "";
    int portnum = 0;
    int udpport = 0;
    int wait = 0;
    float chunksleep = 0;
    int nchunks = 100;
    vector<int> beams;
    int web_port = 8081;
    
    int c;
    while ((c = getopt(argc, argv, "a:p:b:c:u:ws:n:h")) != -1) {
        switch (c) {
        case 'a':
            port = string(optarg);
            break;

        case 'p':
            portnum = atoi(optarg);
            break;

        case 'b':
            beams.push_back(atoi(optarg));
            break;

        case 'c':
            web_port = atoi(optarg);
            break;
            
        case 'u':
            udpport = atoi(optarg);
            break;

        case 'w':
            wait = 1;
            break;

        case 's':
            chunksleep = atof(optarg);
            break;

        case 'n':
            nchunks = atoi(optarg);
            break;

        case 'h':
        case '?':
        default:
            cout << string(argv[0]) << ": [-a <address>] [-p <port number>] [-b <beam id>] [-c <web port>] [-u <L1 udp-port>] [-w to wait indef] [-s <sleep between chunks>] [-n <N chunks>] [-h for help]" << endl;
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

    if (beams.size() == 0) {
        beams.push_back(77);
    }
    
    intensity_network_stream::initializer ini;
    for (int beam: beams)
        ini.beam_ids.push_back(beam);
    ini.nupfreq = nupfreq;
    ini.nt_per_packet = nt_per;
    ini.fpga_counts_per_sample = fpga_per;
    //ini.force_fast_kernels = HAVE_AVX2;

    ini.telescoping_ringbuf_capacity.push_back(4);
    ini.telescoping_ringbuf_capacity.push_back(4);
    ini.telescoping_ringbuf_capacity.push_back(4);
    ini.telescoping_ringbuf_capacity.push_back(4);
    
    if (udpport)
        ini.udp_port = udpport;

    output_device::initializer out_params;
    out_params.device_name = "";
    // debug
    out_params.verbosity = 3;
    std::shared_ptr<output_device> outdev = output_device::make(out_params);

    ini.output_devices.push_back(outdev);

    shared_ptr<intensity_network_stream> stream = intensity_network_stream::make(ini);
    stream->start_stream();

    shared_ptr<CivetServer> webserver = start_web_server(web_port, stream);
    if (!webserver) {
        return -1;
    }
    cout << "Started web server on port " << web_port << endl;
    
    // listen on localhost only, for local-machine testing (trying to
    // listen on other ports triggers GUI window popup warnings on Mac
    // OSX)
    if ((port.length() == 0) && (portnum == 0))
        port = "tcp://127.0.0.1:5555";
    else if (portnum)
        port = "tcp://127.0.0.1:" + to_string(portnum);

    chlog("Starting RPC server on port " << port);
    L1RpcServer rpc(stream, port);
    std::thread rpc_thread = rpc.start();

    std::random_device rd;
    std::mt19937 rng(rd());
    rng.seed(42);
    std::uniform_int_distribution<> rando(0,1);

    std::vector<int> consumed_chunks;

    int backlog = 0;
    int failed_push = 0;

    for (int i=0; i<nchunks; i++) {
	assembled_chunk::initializer ini_params;
        for (int b: beams) {
            ini_params.beam_id = b;
            ini_params.nupfreq = nupfreq;
            ini_params.nt_per_packet = nt_per;
            ini_params.fpga_counts_per_sample = fpga_per;
            ini_params.ichunk = i;

            unique_ptr<assembled_chunk> uch = assembled_chunk::make(ini_params);
            assembled_chunk* ch = uch.release();
            chlog("Injecting " << i);
            if (stream->inject_assembled_chunk(ch))
                chlog("Injected " << i);
            else {
                chlog("Inject failed (ring buffer full)");
                failed_push++;
            }

            // downstream thread consumes with a lag of 2...
            if (i >= 2) {
                // Randomly consume 0 to 2 chunks
                shared_ptr<assembled_chunk> ach;
                if ((backlog > 0) && rando(rng)) {
                    cout << "Downstream consumes a chunk (backlog)" << endl;
                    ach = stream->get_assembled_chunk(0, false);
                    if (ach) {
                        cout << "  (chunk " << ach->ichunk << ")" << endl;
                        consumed_chunks.push_back(ach->ichunk);
                        backlog--;
                    }
                }
                if (rando(rng)) {
                    chlog("Downstream consumes a chunk");
                    ach = stream->get_assembled_chunk(0, false);
                    if (ach) {
                        chlog("  (chunk " << ach->ichunk << ")");
                        consumed_chunks.push_back(ach->ichunk);
                    } else
                        backlog++;
                }
                if (rando(rng)) {
                    chlog("Downstream consumes a chunk");
                    ach = stream->get_assembled_chunk(0, false);
                    if (ach) {
                        chlog("  (chunk " << ach->ichunk << ")");
                        consumed_chunks.push_back(ach->ichunk);
                    } else
                        backlog++;
                }
            }
        }

        cout << endl;
        stream->print_state();
        cout << endl;


        if (chunksleep)
            usleep(int(chunksleep * 1000000));

    }

    //cout << "End state:" << endl;
    //rb->print();
    //cout << endl;

    vector<vector<pair<shared_ptr<assembled_chunk>, uint64_t> > > chunks;
    chlog("Test retrieving chunks...");
    //rb->retrieve(30000000, 50000000, chunks);

    //vector<int> beams;
    //beams.push_back(beam);
    chunks = stream->get_ringbuf_snapshots(beams);
    stringstream ss;
    ss << "Got " << chunks.size() << " beams, with number of chunks:";
    for (auto it = chunks.begin(); it != chunks.end(); it++) {
        ss << " " << it->size();
    }
    string s = ss.str();
    chlog(s);


    int Nchunk = 1024 * 400;
    for (auto it = chunks.begin(); it != chunks.end(); it++) {
        cout << "[" << endl;
        for (auto it2 = it->begin(); it2 != it->end(); it2++) {
            shared_ptr<assembled_chunk> ch = it2->first;
            cout << "  chunk " << (ch->fpga_begin / Nchunk) << " to " <<
                (ch->fpga_end / Nchunk) << ", N chunks " <<
                ((ch->fpga_end - ch->fpga_begin) / Nchunk) << endl;
        }
        cout << "]" << endl;
    }

    cout << "State:" << endl;
    stream->print_state();


    for (int nwait=0;; nwait++) {
        if (rpc.is_shutdown())
            break;
        usleep(1000000);
        if (!wait && nwait >= 30)
            break;
    }
    chlog("Exiting");

    rpc.do_shutdown();
    rpc_thread.join();
}


/*
int main() {
    cout << "Creating ringbuf..." << endl;
    Ringbuf<int> rb(4);

    int a = 42;
    int b = 43;
    int c = 44;

    cout << "Pushing" << endl;
    rb.push(&a);
    cout << "Pushing" << endl;
    rb.push(&b);
    cout << "Pushing" << endl;
    rb.push(&c);

    cout << "Popping" << endl;
    shared_ptr<int> p1 = rb.pop();
    cout << "Popping" << endl;
    shared_ptr<int> p2 = rb.pop();
    cout << "Dropping" << endl;
    p1.reset();
    cout << endl;

    int d = 45;
    int e = 46;
    int f = 47;
    int g = 48;

    cout << "Pushing d..." << endl;
    shared_ptr<int> pd = rb.push(&d);

    cout << endl;
    cout << "Pushing e..." << endl;
    shared_ptr<int> pe = rb.push(&e);

    cout << endl;
    cout << "Pushing f..." << endl;
    shared_ptr<int> pf = rb.push(&f);

    cout << endl;
    cout << "Pushing g..." << endl;
    rb.push(&g);

    cout << "Done" << endl;

}
 */

