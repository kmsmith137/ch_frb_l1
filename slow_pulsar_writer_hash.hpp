#ifndef _SLOW_PULSAR_WRITER_HASH_HPP	
#define _SLOW_PULSAR_WRITER_HASH_HPP

#include <mutex>
#include <rf_pipelines.hpp>
#include <mask_stats.hpp>

namespace ch_frb_l1 {
#if 0
}  // compiler pacifier
#endif

// A thread-safe hash table (beam_id) -> (slow_pulsar_writer)
// FIXME needs argument checking, informative error messages!

class slow_pulsar_writer_hash {
public:
    using sp_writer = std::shared_ptr<rf_pipelines::chime_slow_pulsar_writer>;

    void set(int beam_id, const std::string &source, const sp_writer &sp)
    {
	std::lock_guard<std::mutex> ulock(_mutex);
	_hash[{beam_id, source}] = sp;
    }

    sp_writer get(int beam_id, const std::string &source)
    {
	std::lock_guard<std::mutex> ulock(_mutex);
	return _hash[{beam_id, source}];
    }
    
protected:
    std::unordered_map<std::pair<int, std::string>, sp_writer, pair_hash> _hash; 
    std::mutex _mutex;
};


}  // namespace ch_frb_l1

#endif
