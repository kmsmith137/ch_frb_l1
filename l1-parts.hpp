#ifndef CH_FRB_L1_PARTS_H
#define CH_FRB_L1_PARTS_H

#include <string>
#include <vector>
#include <unordered_map>

#include <msgpack.hpp>
#include <zmq.hpp>

#include <rf_pipelines.hpp>
#include <bonsai.hpp>

class my_coarse_trigger_set { //: public bonsai::coarse_trigger_set {
public:
    //my_coarse_trigger_set(const bonsai::coarse_trigger_set &t);
    my_coarse_trigger_set();

    // The coarse DM index 'i' corresponds to DM range:
    //    i * (max_dm/ndm_coarse) <= DM <= (i+1) * (max_dm/ndm_coarse)

    // The time index needs a little more explanation.  For purposes of triggering, the "arrival time"
    // of an FRB is the time when it arrives in the lowest frequency band.  Additionally, the trigger
    // has a small time offset applied (the trigger is slightly delayed relative to the FRB) for
    // technical convenience in the assembly language kernels.  This time offset is usually less than
    // one coarse-grained trigger.

    // The coarse time index 'i' corresponds to pulse arrival time range (relative to the
    // beginning of the chunk):
    //
    //    i*dt0 - trigger_lag_dt  <= t <= (i+1)*dt0 - trigger_lag_dt
    //
    // where dt0 = (nt_chunk * dt_sample) / nt_coarse_per_chunk is the time duration of one
    // coarse-grained trigger.
    
    int version;
    double t0;
    uint64_t fpgacounts0;
    float max_dm;
    float dt_sample;
    float trigger_lag_dt;
    int nt_chunk;
    int dm_coarse_graining_factor;
    int ndm_coarse;
    int ndm_fine;
    int nt_coarse_per_chunk;
    int nsm;
    int nbeta;
    int tm_stride_dm;
    int tm_stride_sm;
    int tm_stride_beta;
    int ntr_tot;
    std::vector<float> trigger_vec;

    MSGPACK_DEFINE(version, t0, fpgacounts0, max_dm, dt_sample, trigger_lag_dt,
                   nt_chunk, dm_coarse_graining_factor,
                   ndm_coarse, ndm_fine, nt_coarse_per_chunk, nsm, nbeta,
                   tm_stride_dm, tm_stride_sm, tm_stride_beta, ntr_tot, trigger_vec);
};

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

    void pack(msgpack::sbuffer &buffer, int index);
    
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
