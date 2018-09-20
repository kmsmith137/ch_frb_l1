#include <algorithm>
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

    /*
     unordered_map<string, float> stats = get_stats(5.);
     for (const auto &it : stats) {
     cout << "  " << it.first << " = " << it.second << endl;
     }
     */
}

vector<rf_pipelines::mask_counter_measurements>
mask_stats::get_all_measurements() {
    vector<rf_pipelines::mask_counter_measurements> copy;
    {
        ulock l(_meas_mutex);
        // Reorder the ring buffer.
        int n = _meas.size();
        for (int off=0; off<n; off++)
            copy.push_back(_meas[(_imeas + 1 + off) % n]);
    }
    return copy;
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
    float tot_f = 0;
    {
        ulock l(_meas_mutex);
        if (nsteps > _meas.size())
            nsteps = _meas.size();
        int istart = (_imeas - nsteps + _maxmeas) % _maxmeas;
        //cout << "History: " << history << " imeas=" << _imeas << ", max="
        //<< _maxmeas << " -> istart " << istart << endl;
        //cout << "Gathering stats" << endl;
        for (int offset=0; offset<nsteps; offset++) {
            int i = (istart + offset) % _maxmeas;
            totsamp += _meas[i].nsamples;
            totmasked += _meas[i].nsamples_masked;
            tot_t += _meas[i].nt;
            tot_tmasked += _meas[i].nt_masked;
            tot_f += _meas[i].nf;
            tot_fmasked += _meas[i].nf_masked;
            //cout << "  pos " << _meas[i].pos << " ns " << _meas[i].nsamples_masked << "/" <<_meas[i].nsamples << ", nt " << _meas[i].nt_masked << "/" << _meas[i].nt << ", nf " << _meas[i].nf_masked << "/" << _meas[i].nf << endl;
        }
    }
    //cout << "Percentages: " << (100. * totmasked / max(totsamp, 1.f))
    //<< ", " << (100. * tot_tmasked / max(tot_t, 1.f))
    //<< ", " << (100. * tot_fmasked / max(tot_f, 1.f)) << endl;

    stats["rfi_mask_pct_masked"]   = 100. * totmasked / max(totsamp, 1.f);
    stats["rfi_mask_pct_t_masked"] = 100. * tot_tmasked / max(tot_t, 1.f);
    stats["rfi_mask_pct_f_masked"] = 100. * tot_fmasked / max(tot_f, 1.f);
    return stats;
}
    
