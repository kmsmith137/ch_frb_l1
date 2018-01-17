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
            mg_printf(conn, "%s{beam=\"%i\"} %lu\n", name, (int)bstats["beam_id"], bstats["ringbuf_fpga_min"]);
        }

        name = "ringbuf_fpga_max";
        mg_printf(conn,
                  "# HELP %s Largest FPGA-counts timestamp in the ring buffer, per beam\n"
                  "# TYPE %s gauge\n", name, name);
        for (size_t i=2; i<stats.size(); i++) {
            unordered_map<string, uint64_t> bstats = stats[i];
            mg_printf(conn, "%s{beam=\"%i\"} %lu\n", name, (int)bstats["beam_id"], bstats["ringbuf_fpga_max"]);
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
            mg_printf(conn, "%s{beam=\"%i\",level=\"%i\"} %lu\n", name, key.first, (int)key.second, it->second);
        }
        name = "ringbuf_level_fpga_max";
        mg_printf(conn,
                  "# HELP %s Largest FPGA-counts timestamp in the ring buffer, per beam AND binning\n"
                  "# TYPE %s gauge\n", name, name);
        for (auto it=maxfpgas.begin(); it!=maxfpgas.end(); it++) {
            auto key = it->first;
            mg_printf(conn, "%s{beam=\"%i\",level=\"%i\"} %lu\n", name, key.first, (int)key.second, it->second);
        }
        name = "ringbuf_level_nchunks";
        mg_printf(conn,
                  "# HELP %s Number of chunks in the ring buffer, per beam AND binning\n"
                  "# TYPE %s gauge\n", name, name);
        for (auto it=nchunks.begin(); it!=nchunks.end(); it++) {
            auto key = it->first;
            mg_printf(conn, "%s{beam=\"%i\",level=\"%i\"} %lu\n", name, key.first, (int)key.second, it->second);
        }
        
        return true;
    }

protected:
    shared_ptr<intensity_network_stream> _stream;
};

/*
class PrometheusServer {
public:
    PrometheusServer(shared_ptr<CivetServer> cs) :
        _civet(cs) {}
protected:
};
 */
class PrometheusServer : public CivetServer {};

shared_ptr<CivetServer> start_prometheus_server(string ipaddr, int port,
                                                shared_ptr<intensity_network_stream> stream) {
    //"document_root", DOCUMENT_ROOT, "listening_ports", PORT, 0};
    std::vector<std::string> options;
    // listening_ports = [ipaddr:]port
    options.push_back("listening_ports");
    if (ipaddr.size())
        options.push_back(ipaddr + ":" + to_string(port));
    else
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
