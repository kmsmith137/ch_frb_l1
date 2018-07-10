#ifndef _MASK_STATS_HPP
#define _MASK_STATS_HPP

#include <mutex>
#include <rf_pipelines.hpp>

namespace ch_frb_l1 {
#if 0
}  // compiler pacifier
#endif

class mask_stats : public rf_pipelines::mask_counter_callback {
public:
    mask_stats(int beam_id, std::string where="", int nhistory=60);
    virtual void mask_count(const struct rf_pipelines::mask_counter_measurements& m);
    virtual ~mask_stats();
    std::unordered_map<std::string, float> get_stats(float period);

    const int _beam_id;
    std::string _where;
private:
    std::mutex _meas_mutex;
    std::vector<rf_pipelines::mask_counter_measurements> _meas;
    int _imeas;
    int _maxmeas;
};



// Required to use a pair<int,string> as a key in an unordered_map.
// From https://stackoverflow.com/questions/32685540/unordered-map-with-pair-as-key-not-compiling
struct pair_hash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1,T2> &p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ h2;  
    }
};

// This is a map of <int beam_id, string where> to shared_ptr<mask_stats>
// "where" is a property (specified in the JSON) of the mask_counter_transform
typedef std::unordered_map<std::pair<int,std::string>, std::shared_ptr<ch_frb_l1::mask_stats>, pair_hash> mask_stats_map;


}  // namespace ch_frb_l1

#endif
