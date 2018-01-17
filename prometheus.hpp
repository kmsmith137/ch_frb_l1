#ifndef L1_PROMETHEUS_H_
#define L1_PROMETHEUS_H_

#include <memory>

#include <ch_frb_io.hpp>

class PrometheusServer;

std::shared_ptr<PrometheusServer> start_prometheus_server(std::string ipaddr, int port,
                                                   std::shared_ptr<ch_frb_io::intensity_network_stream> st);


#endif
