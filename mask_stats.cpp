#include <iostream>
#include <mask_stats.hpp>

using namespace std;
using namespace rf_pipelines;
using namespace ch_frb_io;
using namespace ch_frb_l1;

mask_stats::mask_stats() {
}

mask_stats::~mask_stats() {
}

void mask_stats::mask_count(const struct rf_pipelines::mask_counter_measurements& meas) {
    cout << "mask_stats: got pos " << meas.pos << ": N samples masked: " << meas.nsamples_masked << "/" << meas.nsamples << "; n times " << meas.nt_masked << "/" << meas.nt << "; n freqs " << meas.nf_masked << "/" << meas.nf << endl;
}

