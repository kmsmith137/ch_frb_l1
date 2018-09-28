#include <algorithm>
#include <iostream>
#include <mask_stats.hpp>

using namespace std;
using namespace rf_pipelines;
using namespace ch_frb_io;
using namespace ch_frb_l1;

typedef lock_guard<mutex> ulock;

void mask_stats_map::put(int beam_id, std::string where,
                         std::shared_ptr<rf_pipelines::mask_measurements_ringbuf> ringbuf) {
    {
        ulock l(mutex);
        map[make_pair(beam_id, where)] = ringbuf;
    }
}
