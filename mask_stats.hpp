#ifndef _MASK_STATS_HPP
#define _MASK_STATS_HPP

#include <mutex>
#include <rf_pipelines.hpp>

namespace ch_frb_l1 {
#if 0
}  // compiler pacifier
#endif

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

class mask_stats_map {
public:
    // This is a map of <int stream_index, string where> to shared_ptr<mask_measurements_ringbuf>
    // "where" is a property (specified in the JSON) of the mask_counter_transform
    typedef std::unordered_map<std::pair<int,std::string>, std::shared_ptr<rf_pipelines::mask_measurements_ringbuf>, pair_hash> msm;

    msm map;

    // A note about the threading: on startup, the dedispersion threads each
    // parse the RFI chain JSON file, look for mask_counter stages, and add
    // their mask-measuring ring buffers to this dictionary.  These all happen
    // nearly at once, so are mutex-protected.  This dictionary then becomes
    // read-only, read by the l1-prometheus and l1-rpc services (threads).
    // We pass const references and blithely assume that this is ok, that
    // multiple readers can access the unordered_map without problem.
    void put(int, std::string, std::shared_ptr<rf_pipelines::mask_measurements_ringbuf>);
    //std::shared_ptr<rf_pipelines::mask_measurements_ringbuf> get(int, std::string) const;

    size_t size() const {
        return map.size();
    }
protected:
    std::mutex mutex;
};


// Required to make shared_ptr<mask_stats_map> work in a "range-based for" loop
template<typename T>
auto inline begin(std::shared_ptr<T> ptr) -> typename T::iterator {
    return ptr->begin();
}
template<typename T>
auto inline end(std::shared_ptr<T> ptr) -> typename T::iterator {
    return ptr->end();
}

}  // namespace ch_frb_l1

#endif
