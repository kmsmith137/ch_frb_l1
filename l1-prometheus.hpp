#ifndef L1_PROMETHEUS_H_
#define L1_PROMETHEUS_H_

#include <memory>

#include <ch_frb_io.hpp>

class L1PrometheusServer;

// *ipaddr_port*: [ipaddr:]port
std::shared_ptr<L1PrometheusServer> start_prometheus_server(std::string ipaddr_port,
                                                          std::shared_ptr<ch_frb_io::intensity_network_stream> st);


#endif
