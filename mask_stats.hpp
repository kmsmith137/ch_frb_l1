#ifndef _MASK_STATS_HPP
#define _MASK_STATS_HPP

#include <rf_pipelines.hpp>

namespace ch_frb_l1 {
#if 0
}  // compiler pacifier
#endif

class mask_stats : public rf_pipelines::mask_counter_callback {
public:
    mask_stats();
    virtual void mask_count(const struct rf_pipelines::mask_counter_measurements& m);
    virtual ~mask_stats();
};

}  // namespace ch_frb_l1

#endif
