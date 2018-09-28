#ifndef L1_PROMETHEUS_H_
#define L1_PROMETHEUS_H_

#include <memory>

#include <ch_frb_io.hpp>

#include <mask_stats.hpp>

class L1PrometheusServer;

// *ipaddr_port*: [ipaddr:]port
std::shared_ptr<L1PrometheusServer> start_prometheus_server(std::string ipaddr_port,
                                                            std::shared_ptr<ch_frb_io::intensity_network_stream> st,
                                                            std::shared_ptr<ch_frb_l1::mask_stats_map> ms);

#endif
