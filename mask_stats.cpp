#include <iostream>
#include <mask_stats.hpp>

using namespace std;
using namespace rf_pipelines;
using namespace ch_frb_io;
using namespace ch_frb_l1;

typedef lock_guard<mutex> ulock;

mask_stats::mask_stats(int beam_id, string where, int nhistory) :
    _beam_id(beam_id),
    _where(where),
    _imeas(0),
    _maxmeas(nhistory)
{}

mask_stats::~mask_stats() {
}

void mask_stats::mask_count(const struct rf_pipelines::mask_counter_measurements& meas) {
    cout << "mask_stats: beam " << _beam_id << " got pos " << meas.pos << ": N samples masked: " << meas.nsamples_masked << "/" << meas.nsamples << "; n times " << meas.nt_masked << "/" << meas.nt << "; n freqs " << meas.nf_masked << "/" << meas.nf << endl;

    {
        ulock l(_meas_mutex);
        if (_imeas < _maxmeas)
            _meas.push_back(meas);
        else
            // ring buffer
            _meas[_imeas] = meas;
        _imeas = (_imeas + 1) % _maxmeas;
    }
}

unordered_map<string, float> mask_stats::get_stats(float history) {
    unordered_map<string, float> stats;

    // FIXME -- assume one sample per second!
    int nsteps = (int)history;

    float totsamp = 0;
    float totmasked = 0;
    float tot_t = 0;
    float tot_tmasked = 0;
    float tot_fmasked = 0;
    {
        ulock l(_meas_mutex);
        if (nsteps > _meas.size())
            nsteps = _meas.size();
        int istart = (_imeas - nsteps + _maxmeas) % _maxmeas;
        int iend = (istart + nsteps) % _maxmeas;
        cout << "History: " << history << " imeas=" << _imeas << ", max="
             << _maxmeas << " -> istart " << istart << ", iend " << iend << endl;
        for (int i=istart; i<(istart + nsteps); i=(i + 1) % _maxmeas) {
            totsamp += _meas[i].nsamples;
            totmasked += _meas[i].nsamples_masked;
            tot_t += _meas[i].nt;
            tot_tmasked += _meas[i].nt_masked;
            tot_fmasked += _meas[i].nf_masked;
        }
    }
    stats["rfi_mask_pct_masked"] = 100. * totmasked / totsamp;
    stats["rfi_mask_pct_t_masked"] = 100. * tot_tmasked / tot_t;
    stats["rfi_mask_pct_f_masked"] = 100. * tot_fmasked / (float)nsteps;
    return stats;
}
    
