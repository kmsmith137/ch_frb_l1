#ifndef CH_FRB_L1_PARTS_H
#define CH_FRB_L1_PARTS_H

#include <string>
#include <vector>
#include <unordered_map>

#include <msgpack.hpp>
#include <zmq.hpp>

#include <rf_pipelines.hpp>
#include <bonsai.hpp>

class msgpack_config_serializer : public bonsai::config_serializer {
public:
    msgpack_config_serializer();
    virtual ~msgpack_config_serializer();

    virtual void write_param(const std::string &key, int val);
    virtual void write_param(const std::string &key, double val);
    virtual void write_param(const std::string &key, const std::string &val);
    virtual void write_param(const std::string &key, const std::vector<int> &val);
    virtual void write_param(const std::string &key, const std::vector<double> &val);
    virtual void write_param(const std::string &key, const std::vector<std::string> &val);
    virtual void write_analytic_variance(const float *in, const std::vector<int> &shape, int itree);
    
    int size();

    void pack(msgpack::packer<msgpack::sbuffer> &packer, int index, int nextra);
    
protected:
    // All arrays are assumed to be the same size.
    int sz;
    std::unordered_map<std::string, int> vals_i;
    std::unordered_map<std::string, double> vals_d;
    std::unordered_map<std::string, std::string> vals_s;
    std::unordered_map<std::string, std::vector<int> > vals_ivec;
    std::unordered_map<std::string, std::vector<double> > vals_dvec;
    std::unordered_map<std::string, std::vector<std::string> > vals_svec;
};

class l1b_trigger_stream : public bonsai::trigger_output_stream {
public:
    l1b_trigger_stream(zmq::context_t* ctx, std::string addr,
                       bonsai::config_params bc);
    virtual ~l1b_trigger_stream();

    virtual void start_processor() override { }
    virtual void process_triggers(const std::vector<std::shared_ptr<bonsai::coarse_trigger_set>> &triggers, int ichunk) override;
    virtual void end_processor() override { }
protected:
    zmq::context_t* zmqctx;
    zmq::socket_t socket;
    bonsai::config_params bonsai_config;
    msgpack_config_serializer config_headers;
};


std::vector<std::shared_ptr<rf_pipelines::wi_transform> > make_rfi_chain();

std::shared_ptr<rf_pipelines::wi_transform> make_dedisperser(const bonsai::config_params &cp, const std::shared_ptr<bonsai::trigger_output_stream> &tp);


#endif
