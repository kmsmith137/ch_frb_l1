#include <thread>
#include "l1-parts.hpp"
#include "chlog.hpp"

using namespace std;

// make_rfi_chain(): currently a placeholder which returns an arbitrarily constructed transform chain.
//
// The long-term plan here is:
//   - keep developing RFI removal, until all transforms are C++
//   - write code to serialize a C++ transform chain to yaml
//   - add a command-line argument <transform_chain.yaml> to ch-frb-l1 

// A little helper routine to make the bonsai_dedisperser 
// (Returns the rf_pipelines::wi_transform wrapper object, not the bonsai::dedisperser)
shared_ptr<rf_pipelines::wi_transform> make_dedisperser(const bonsai::config_params &cp, const shared_ptr<bonsai::trigger_output_stream> &tp)
{
    bonsai::dedisperser::initializer ini_params;
    ini_params.verbosity = 0;
    
    auto d = make_shared<bonsai::dedisperser> (cp, ini_params);
    d->add_processor(tp);

    return rf_pipelines::make_bonsai_dedisperser(d);
}

vector<shared_ptr<rf_pipelines::wi_transform>> make_rfi_chain()
{
    int nt_chunk = 1024;
    int polydeg = 2;

    auto t1 = rf_pipelines::make_polynomial_detrender(nt_chunk, rf_pipelines::AXIS_FREQ, polydeg);
    auto t2 = rf_pipelines::make_polynomial_detrender(nt_chunk, rf_pipelines::AXIS_TIME, polydeg);
    
    return { t1, t2 };
}

my_coarse_trigger_set::my_coarse_trigger_set() {}

/*
my_coarse_trigger_set::my_coarse_trigger_set(const bonsai::coarse_trigger_set& t)
 */

l1b_trigger_stream::l1b_trigger_stream(zmq::context_t* ctx, string addr,
                                       bonsai::config_params bc) :
    zmqctx(ctx ? NULL : new zmq::context_t()),
    socket(*zmqctx, ZMQ_PUB),
    bonsai_config(bc)
{
    cout << "Connecting socket to L1b at " << addr << endl;
    socket.connect(addr);
}

l1b_trigger_stream::~l1b_trigger_stream() {
    socket.close();
    if (zmqctx)
        delete zmqctx;
}

// helper for the next function
static void myfree(void* p, void*) {
    ::free(p);
}
// Convert a msgpack buffer to a ZeroMQ message; the buffer is released.
static zmq::message_t* sbuffer_to_message(msgpack::sbuffer &buffer) {
    zmq::message_t* msg = new zmq::message_t(buffer.data(), buffer.size(), myfree);
    buffer.release();
    return msg;
}

void l1b_trigger_stream::process_triggers(const std::vector<std::shared_ptr<bonsai::coarse_trigger_set> > &triggers, int ichunk) {
    msgpack::sbuffer buffer;
    assert(triggers.size() == bonsai_config.trigger_lag_dt.size());
    vector<my_coarse_trigger_set> mytriggers;
    int i=0;
    for (auto it=triggers.begin(); it != triggers.end();
         it++, i++) {
        my_coarse_trigger_set mt;
        mt.version = 1;
        mt.t0 = (*it)->t0;
        mt.fpgacounts0 = mt.t0 / rf_pipelines::constants::chime_seconds_per_fpga_count;
        mt.max_dm = bonsai_config.max_dm[i];
        mt.dt_sample = bonsai_config.dt_sample;
        mt.trigger_lag_dt = bonsai_config.trigger_lag_dt[i];
        mt.nt_chunk = bonsai_config.nt_chunk;
        mt.dm_coarse_graining_factor = (*it)->dm_coarse_graining_factor;
        mt.ndm_coarse = (*it)->ndm_coarse;
        mt.ndm_fine = (*it)->ndm_fine;
        mt.nt_coarse_per_chunk = (*it)->nt_coarse_per_chunk;
        mt.nsm = (*it)->nsm;
        mt.nbeta = (*it)->nbeta;
        mt.tm_stride_dm = (*it)->tm_stride_dm;
        mt.tm_stride_sm = (*it)->tm_stride_sm;
        mt.tm_stride_beta = (*it)->tm_stride_beta;
        mt.ntr_tot = mt.ndm_coarse * mt.nsm * mt.nbeta * mt.nt_coarse_per_chunk;
        mt.trigger_vec = vector<float>(mt.ntr_tot);

        float* triggers_data = &mt.trigger_vec[0];
        memcpy(triggers_data, (*it)->triggers, mt.ntr_tot * sizeof(float));
        mytriggers.push_back(mt);
    }
    msgpack::pack(buffer, mytriggers);
    zmq::message_t* reply = sbuffer_to_message(buffer);
    chlog("Sending message of size: " << reply->size() << " to L1b");
    socket.send(*reply);
}

