#include <unistd.h>
#include <iostream>
#include <sstream>
#include <ch_frb_io.hpp>
#include <l1-rpc.hpp>
#include <chlog.hpp>
#include <mask_stats.hpp>

using namespace std;
using namespace ch_frb_io;
using namespace ch_frb_l1;

#include "CivetServer.h"

class L1PrometheusHandler : public CivetHandler {
public:
    L1PrometheusHandler(shared_ptr<intensity_network_stream> stream,
                        shared_ptr<const mask_stats_map> ms) :
        CivetHandler(),
        _stream(stream),
        _mask_stats(ms) {
    }

    bool handleGet(CivetServer *server, struct mg_connection *conn) {
        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/plain\r\n"
                  "Connection: close\r\n\r\n");

        vector<unordered_map<string, uint64_t> > stats = _stream->get_statistics();

        // Stats for the whole stream.
        unordered_map<string, uint64_t> sstats = stats[0];

        sstats["network_thread_waiting_ms"] =
            sstats["network_thread_waiting_usec"] / 1000;
        sstats["network_thread_working_ms"] =
            sstats["network_thread_working_usec"] / 1000;
        sstats["assembler_thread_waiting_ms"] =
            sstats["assembler_thread_waiting_usec"] / 1000;
        sstats["assembler_thread_working_ms"] =
            sstats["assembler_thread_working_usec"] / 1000;
        
        struct metric_stat {
            metric_stat(const char* k1, const char* k2, const char* k3="",
                        const char* k4="gauge") :
                key(k1), metric(k2), help(k3), type(k4) {}
            const char* key;
            const char* metric;
            const char* help;
            const char* type;
        };

        uint64_t slab_bytes = sstats["memory_pool_slab_nbytes"];
        sstats["memory_pool_slab_size_bytes"] = 
            sstats["memory_pool_slab_size"] * slab_bytes;
        sstats["memory_pool_slab_avail_bytes"] = 
            sstats["memory_pool_slab_avail"] * slab_bytes;
        
        struct metric_stat ms[] = {
            {"network_thread_waiting_ms", "l1_network_thread_wait_ms",
             "Cumulative milliseconds spent waiting in the UDP packet-receiving thread"},
            {"network_thread_working_ms", "l1_network_thread_work_ms",
             "Cumulative milliseconds spent working in the UDP packet-receiving thread"},
            {"count_bytes_received", "l1_assembler_received_bytes",
             "Number of bytes received on this L1 UDP socket"},
            {"count_packets_received", "l1_assembler_received_packets",
             "Number of packets received on this L1 UDP socket"},
            {"count_packets_good", "l1_assembler_good_packets",
             "Number of valid packets received on this L1 UDP socket"},
            {"count_assembler_hits", "l1_assembler_hits_packets",
             "Number of packets received in order at the L1 assembler"},
            {"count_assembler_misses", "l1_assembler_misses_packets",
             "Number of packets received out of order at the L1 assembler"},
            {"count_assembler_drops", "l1_assembler_dropped_chunks",
             "Number of data chunks dropped by the L1 assembler"},
            {"count_assembler_queued", "l1_assembler_queued",
             "Number of data chunks sent downstream by the L1 assembler"},
            {"streaming_n_beams", "l1_streaming_beams_count",
             "Streaming data to disk: number of beams being written"},
            //{"output_chunks_queued", "l1_write_queued_chunks",
            //"Number of chunks of data queued for writing"},
            {"memory_pool_slab_size_bytes", "l1_memorypool_capacity_bytes",
             "Number of bytes total capacity in memory pool"},
            {"memory_pool_slab_avail_bytes", "l1_memorypool_avail_bytes",
             "Number of bytes available (free) in memory pool"},
            {"memory_pool_slab_size", "l1_memorypool_capacity_slabs",
             "Total capacity in memory pool, in 'slabs'"},
            {"memory_pool_slab_avail", "l1_memorypool_avail_slabs",
             "Available number in memory pool, in 'slabs'"},
        };
        
        for (size_t i=0; i<sizeof(ms)/sizeof(struct metric_stat); i++) {
            const char* metric = ms[i].metric;
            mg_printf(conn,
                      "# HELP %s %s\n" 
                      "# TYPE %s %s\n"
                      "%s %llu\n", metric, ms[i].help, metric, ms[i].type, metric, (unsigned long long)sstats[ms[i].key]);
        }

        struct metric_stat ms2[] = {
            {"count_packets_bad", "malformed"},
            {"count_packets_dropped", "bufferfull"},
            {"count_packets_beam_id_mismatch", "beamidmismatch"},
            {"count_packets_stream_mismatch", "formatmismatch"},
        };
        const char* key = "l1_assembler_bad_packets";
        mg_printf(conn,
                  "# HELP %s %s\n"
                  "# TYPE %s gauge\n", key,
                  "Number of invalid packets received on L1 UDP socket", key);
        for (size_t i=0; i<sizeof(ms2)/sizeof(struct metric_stat); i++) {
            mg_printf(conn,
                      "%s{reason=\"%s\"} %llu\n", key, ms2[i].metric,
                      (unsigned long long)sstats[ms2[i].key]);
        }

        // output_chunks_queued_X fields (by path prefix)
        key = "l1_write_queued_chunks";
        mg_printf(conn,
                  "# HELP %s %s\n"
                  "# TYPE %s gauge\n",
                  key, "Number of chunks of data queued for writing to a filesystem", key);
        for (auto it = sstats.begin(); it != sstats.end(); it++) {
            string prefix = "output_chunks_queued_";
            if (it->first.substr(0, prefix.size()) != prefix)
                continue;
            string device = it->first.substr(prefix.size());
            mg_printf(conn,
                      "%s{device=\"%s\"} %llu\n", key, device.c_str(),
                      (unsigned long long)it->second);
        }

        // Retrieve and summarize the packet rate history.
        // How many seconds of history to retrieve--should match the
        // prometheus polling interval.
        double period = 15.;
        shared_ptr<packet_counts> packets = _stream->get_packet_rates(-period, period);
        double total_packets = 0;
        int n_nodes = 0;
        if (packets) {
            for (auto it=packets->counts.begin(); it!=packets->counts.end(); it++) {
                if (it->second == 0)
                    continue;
                total_packets += it->second;
                n_nodes++;
            }
            period = packets->period;
        }
        key = "l1_l0_senders_nodes";
        mg_printf(conn,
                  "# HELP %s %s\n"
                  "# TYPE %s gauge\n"
                  "%s %llu\n",
                  key, "Number of L0 nodes that are sending data to this L1 node",
                  key, key, (unsigned long long)n_nodes);
        
        key = "l1_l0_senders_packets";
        mg_printf(conn,
                  "# HELP %s %s\n"
                  "# TYPE %s gauge\n"
                  "%s %llu\n",
                  key, "Total number of packets sent by L0 nodes to this L1 node in this sampling period",
                  key, key, (unsigned long long)total_packets);
        key = "l1_l0_senders_period";
        mg_printf(conn,
                  "# HELP %s %s\n"
                  "# TYPE %s gauge\n"
                  "%s %f\n",
                  key, "Sampling period for packets sent by L0 nodes to this L1 node",
                  key, key, period);
        key = "l1_l0_senders_packetrate";
        mg_printf(conn,
                  "# HELP %s %s\n"
                  "# TYPE %s gauge\n"
                  "%s %f\n",
                  key, "Total number of packets per second sent from L0 nodes to this L1 node in this sampling period",
                  key, key, (period ? total_packets / period : 0.));

        // Stats per beam.

        // Per-beam stats
        struct metric_stat ms4[] = {
            {"streaming_bytes_written", "l1_streaming_written_bytes",
             "Streaming data to disk: number of bytes written"},
            {"streaming_chunks_written", "l1_streaming_written_chunks",
             "Streaming data to disk: number of chunks of data written"},
        };
        for (size_t i=0; i<sizeof(ms4)/sizeof(struct metric_stat); i++) {
            const char* name = ms4[i].metric;
            mg_printf(conn,
                      "# HELP %s %s\n"
                      "# TYPE %s %s\n", name, ms4[i].help, name, ms4[i].type);
            for (size_t ib=2; ib<stats.size(); ib++) {
                unordered_map<string, uint64_t> bstats = stats[ib];
                const char* key = ms4[i].key;
                mg_printf(conn, "%s{beam=\"%i\"} %llu\n", name, (int)bstats["beam_id"], (unsigned long long)bstats[key]);
            }
        }
        
        // Per-beam x per-ringbuffer-level stats
        struct metric_stat ms3[] = {
            {"ringbuf_fpga_min", "l1_ringbuf_min_fpga",
             "Smallest FPGA-counts timestamp in the ring buffer"},
            {"ringbuf_fpga_max", "l1_ringbuf_max_fpga",
             "Largest FPGA-counts timestamp in the ring buffer"},
            {"ringbuf_capacity", "l1_ringbuf_capacity_chunks",
             "Maximum number of chunks of data in the ring buffer"},
            {"ringbuf_ntotal", "l1_ringbuf_size_chunks",
             "Current number of chunks of data in the ring buffer"},
        };

        for (size_t i=0; i<sizeof(ms3)/sizeof(struct metric_stat); i++) {
            const char* name = ms3[i].metric;
            mg_printf(conn,
                      "# HELP %s %s\n"
                      "# TYPE %s %s\n", name, ms3[i].help, name, ms3[i].type);
            for (size_t ib=2; ib<stats.size(); ib++) {
                unordered_map<string, uint64_t> bstats = stats[ib];
                int nlev = bstats["ringbuf_nlevels"];
                const char* key = ms3[i].key;
                mg_printf(conn, "%s{beam=\"%i\",level=\"0\"} %llu\n", name, (int)bstats["beam_id"], (unsigned long long)bstats[key]);
                for (int lev=1; lev<nlev; lev++) {
                    mg_printf(conn, "%s{beam=\"%i\",level=\"%i\"} %llu\n", name, (int)bstats["beam_id"], lev,
                              (unsigned long long)bstats[stringprintf("%s_level%i", key, lev)]);
                }
            }
        }

        // RFI masking stats per-beam.
        if (_mask_stats->size()) {
            struct metric_stat ms5[] = {
                {"rfi_mask_pct_masked", "rfi_mask_pct_masked",
                 "Total fraction of samples masked"},
                /*
                 {"rfi_mask_pct_t_masked", "rfi_mask_pct_times_masked",
                 "Total fraction of times samples that are fully masked"},
                 {"rfi_mask_pct_f_masked", "rfi_mask_pct_frequencies_masked",
                 "Total fraction of frequency channels that are fully masked"},
                 */
            };
            vector<tuple<int, string, unordered_map<string, float> > > maskstats;
            float period = 15.;
            for (const auto &it : _mask_stats->map) {
                // cout << "prometheus mask stats: beam " << it.first.first
                // << " where " << it.first.second << " stats:" << endl;
                unordered_map<string, float> stats = it.second->get_stats(period);
                // cout << "stats: " << stats.size() << endl;
                // for (auto &sit : stats) {
                //     cout << "  " << sit.first << " = " << sit.second << endl;
                // }
                
                maskstats.push_back(make_tuple(it.first.first, it.first.second, it.second->get_stats(period)));
            }
            for (size_t i=0; i<sizeof(ms5)/sizeof(struct metric_stat); i++) {
                const char* name = ms5[i].metric;
                mg_printf(conn,
                          "# HELP %s %s\n"
                          "# TYPE %s %s\n", name, ms5[i].help, name, ms5[i].type);
                std::vector<int> beams = _stream->get_beam_ids();
                for (size_t ib=0; ib<maskstats.size(); ib++) {
                    int stream_ibeam;
                    string where;
                    unordered_map<string, float> mstats;
                    std::tie(stream_ibeam, where, mstats) = maskstats[ib];
                    if (stream_ibeam >= int(beams.size()))
                        continue;
                    int beam_id = beams[stream_ibeam];
                    const char* key = ms5[i].key;
                    mg_printf(conn, "%s{beam=\"%i\",where=\"%s\"} %.2f\n", name,
                              beam_id, where.c_str(), mstats[key]);
                }
            }
        }
        
        return true;
    }

protected:
    shared_ptr<intensity_network_stream> _stream;
    shared_ptr<const ch_frb_l1::mask_stats_map> _mask_stats;
};

class L1PrometheusServer : public CivetServer {
public:
    L1PrometheusServer(std::vector<std::string> options,
                       shared_ptr<intensity_network_stream> stream,
                       shared_ptr<const mask_stats_map> maskstats,
                       const struct CivetCallbacks *callbacks = 0,
                       const void *UserContext = 0) :
        CivetServer(options, callbacks, UserContext) {
        handler = make_shared<L1PrometheusHandler>(stream, maskstats);
        this->addHandler("/metrics", handler.get());
    }
    virtual ~L1PrometheusServer() {
        removeHandler("/metrics");
        handler.reset();
    }
protected:
    std::shared_ptr<L1PrometheusHandler> handler;
};

shared_ptr<L1PrometheusServer> start_prometheus_server(string ipaddr_port,
                                                       shared_ptr<intensity_network_stream> stream,
                                                       shared_ptr<const mask_stats_map> ms) {
    //"document_root", DOCUMENT_ROOT, "listening_ports", PORT, 0};
    std::vector<std::string> options;
    // listening_ports = [ipaddr:]port
    options.push_back("listening_ports");
    options.push_back(ipaddr_port);
    // default is 50, but we're only serving the prometheus poller
    options.push_back("num_threads");
    options.push_back(std::to_string(8));
    shared_ptr<L1PrometheusServer> server;
    try {
        server = make_shared<L1PrometheusServer>(options, stream, ms);
    } catch (CivetException &e) {
        cout << "Failed to start web server on address " << ipaddr_port << ": "
             << e.what() << endl;
        return server;
    }
    return server;
}
