#ifndef CH_FRB_L1_PARTS_H
#define CH_FRB_L1_PARTS_H

#include <string>
#include <vector>
#include <unordered_map>

// #include <msgpack.hpp>
// #include <zmq.hpp>

#include <rf_pipelines.hpp>
#include <bonsai.hpp>

std::vector<std::shared_ptr<rf_pipelines::wi_transform>> make_rfi_chain();

#endif
