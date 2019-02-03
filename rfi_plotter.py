from __future__ import print_function
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.dates as mdates
import pylab as plt
import numpy as np
import datetime
import sqlite3
from rpc_client import SummedMaskedFrequencies

import argparse
parser = argparse.ArgumentParser()
parser.add_argument('rfi_db', nargs=1,
                    help='rfi database filename')
args = parser.parse_args()

dbfn = args.rfi_db[0]
#conn = sqlite3.connect('/data/frb-archiver/dstn/rfi-monitor.db')
# Read-only:
#dbfn = '/data/frb-archiver/dstn/rfi-monitor.db'
#conn = sqlite3.connect('file:'+dbfn + '?mode=ro', uri=True)
#dbfn = '/data/frb-archiver/dstn/rfi-monitor-2019-01-26T15-51-40.db'
conn = sqlite3.connect(dbfn)

db = conn.cursor()

# Frequency vs Time

freqtime = []
dates = []
for row in db.execute('SELECT date, freqs, nt_total FROM rfi_sum ORDER BY date'):
    (date,blob,nt) = row
    freqs = np.frombuffer(blob, dtype='<i8')
    freqtime.append(100. * freqs / nt)
    dates.append(datetime.datetime.strptime(date, '%Y-%m-%dT%H:%M:%S'))
freqtime = np.vstack(freqtime).T
print('Freqtime shape', freqtime.shape)

f_lo, f_hi = 400, 800

date_lo, date_hi = [mdates.date2num(d) for d in [dates[0],dates[-1]]]

plt.clf()
plt.imshow(freqtime, interpolation='nearest', origin='lower', aspect='auto',
           cmap='hot',
           extent=[date_lo, date_hi, f_lo, f_hi])
plt.colorbar()
ax = plt.gca()
ax.xaxis_date()
ax.xaxis.set_major_formatter(mdates.DateFormatter('%m/%d %H:%M'))
fig = plt.gcf()
fig.autofmt_xdate()
plt.ylabel('Frequency (MHz)')
plt.title('RFI masked percentage: Frequency vs Time')
plt.savefig('freq-time.png')

# plt.clim(0, 25)
# plt.ylim(550, 570)
# plt.savefig('freq-time-zoom1.png')
# plt.ylim(650, 670)
# plt.savefig('freq-time-zoom2.png')
# t1 = mdates.date2num(datetime.datetime(2019,1,27,18,0,0))
# t2 = mdates.date2num(datetime.datetime(2019,1,28,0,0,0))
# plt.xlim(t1, t2)
# plt.ylim(550, 570)
# plt.savefig('freq-time-zoom3.png')
# plt.xlim(t1, t2)
# plt.ylim(650, 670)
# plt.savefig('freq-time-zoom4.png')

# Beam vs Time

lastdate = None
beamfrac = None
dates = []
beamdates = []

for row in db.execute('SELECT date, beam, 100.*nsamples_masked/nsamples FROM rfi_meta '
                      'ORDER BY date, beam'):
    (date, beam, fmasked) = row
    if date != lastdate:
        if beamfrac is not None:
            beamdates.append(beamfrac)
            dates.append(lastdate)
        beamfrac = {}
        lastdate = date
    # UNfortunately, "nsamples_masked" is actually "UNmasked"
    #beamfrac[beam] = 100. - fmasked
    beamfrac[beam] = fmasked
if beamfrac is not None:
    beamdates.append(beamfrac)
    dates.append(lastdate)
allbeams = set()
for beamfrac in beamdates:
    allbeams.update(beamfrac.keys())
print('Got', len(dates), 'dates x', len(allbeams), 'total beams')
print('All beams:', allbeams)
beam_index = dict([(b,i) for i,b in enumerate(allbeams)])

beam_times = np.zeros((len(allbeams), len(beamdates)))
for idate,beamfrac in enumerate(beamdates):
    for beam,f in beamfrac.items():
        ibeam = beam_index[beam]
        beam_times[ibeam, idate] = f

dateobjs = [datetime.datetime.strptime(iso, '%Y-%m-%dT%H:%M:%S')
            for iso in dates]
# matplotlib numerical dates
ndates = [mdates.date2num(d) for d in dateobjs]

plt.clf()
plt.imshow(beam_times, interpolation='nearest', origin='lower', cmap='hot', aspect='auto',
           extent=[ndates[0], ndates[-1], 0, len(allbeams)-1])
plt.colorbar()
ax = plt.gca()
ax.xaxis_date()
ax.xaxis.set_major_formatter(mdates.DateFormatter('%m/%d %H:%M'))
fig = plt.gcf()
fig.autofmt_xdate()
plt.xlabel('Date (UTC)')
plt.ylabel('Beam')
plt.title('RFI masked percentage, by beam')
plt.savefig('beam-time.png')

# Latest sample: beam vs freq

db.execute('SELECT max(date) from rfi')
latest = db.fetchone()
print('Got latest date:', latest)

allbeams = []
for row in db.execute("SELECT * FROM rfi WHERE date=?", latest):

    (date, beam, frame0nano, fpga_start, fpga_end, sample_start, nt, nsamples,
     nsamples_masked, blob) = row

    # NOTE, these frequencies are not flipped (they're in 400..800 order)
    freqs = np.frombuffer(blob, dtype='<i4')
    #print('Got freqs:', freqs.dtype, freqs.shape)
    nf = len(freqs)
    #print(freqs)
    r = SummedMaskedFrequencies(beam, fpga_start, fpga_end, sample_start, nt,
                                nf, nsamples, nsamples_masked, freqs)
    allbeams.append(r)

print('Parsed', len(allbeams), 'beams for latest date')

beams = [r.beam for r in allbeams]

bw = 1
bh = 1 + max(beams)
beam_map = np.zeros((bh,bw))
for r in allbeams:
    bx,by = 0, r.beam
    beam_map[by,bx] = float(r.nsamples_masked) / float(r.nsamples)

f_lo,f_hi = 400.,800.

plt.clf()
beamfreqs = []
for r in allbeams:
    beamfreqs.append(r.freqs_masked)
beamfreqs = np.vstack(beamfreqs)
nb,nf = beamfreqs.shape
plt.imshow(beamfreqs, interpolation='nearest', origin='lower',
           extent=[f_lo,f_hi,0,nb], cmap='hot', aspect='auto')
plt.xlim(f_lo, f_hi)
plt.xlabel('Frequency (MHz)')
plt.ylabel('Beam number')
plt.suptitle('Fraction masked, by beam x frequency')
plt.savefig('beamfreq.png')



'''
    if False:

        sumhist = 0
        sumt = 0
        for r in allbeams:
            print(str(r))
            if not r.beam in beam_histories:
                beam_histories[r.beam] = []
            # Frequency ordering flip!
            f = np.flip(r.freqs_masked, 0)
            beam_histories[r.beam].append(f.astype(np.float32) / float(r.nt))
            sumhist = sumhist + f
            sumt += r.nt
    
        bw = 1
        bh = 1 + max(beam_histories.keys())
        beam_map = np.zeros((bh,bw))
        for r in allbeams:
            bx,by = 0, r.beam
            beam_map[by,bx] = float(r.nsamples_masked) / float(r.nsamples)
    
        #print('Beam map:', beam_map)
    
        if sumt > 0:
            sum_history.append(sumhist.astype(np.float32) / float(sumt))
            map_history.append(np.sum(beam_map, axis=1))
    
        f_lo,f_hi = 400.,800.
    
        if False:
            plt.clf()
            nplots = len(beam_histories)
            plt.subplots_adjust(hspace=0.1)
            for i,(b,hh) in enumerate(beam_histories.items()):
                latest = hh[-1]
                plt.subplot(nplots, 1, i+1)
                #plt.plot(np.linspace(400, 800, len(latest)), latest, '-', label='Beam '+str(b))
        
                #plt.fill_between(np.linspace(400, 800, len(latest)), 0, latest, linestyle='-', label='Beam '+str(b))
                plt.fill_between(np.linspace(f_lo, f_hi, len(latest)), 0, latest, lw=0., alpha=0.5, label='Beam '+str(b))
                if i != nplots-1:
                    plt.xticks([])
                plt.xlim(f_lo, f_hi)
                plt.ylim(0, 1)
                plt.yticks([])
                plt.axhline(1., color='k', alpha=0.3)
                plt.ylabel('Beam ' + str(b))
            #plt.legend()
            plt.xlabel('Frequency (MHz)')
            plt.suptitle('Fraction masked, by beam')
            plt.savefig('latest.png')
    
        plt.clf()
        nplots = len(beam_histories)
        for i,(b,hh) in enumerate(beam_histories.items()):
            latest = hh[-1]
            plt.fill_between(np.linspace(f_lo, f_hi, len(latest)), 0, latest, lw=0., alpha=0.8/float(nplots), label='Beam '+str(b))
        plt.xlim(f_lo, f_hi)
        plt.ylim(-0.05, 1.05)
        #plt.axhline(0., color='k', alpha=0.3)
        #plt.axhline(1., color='k', alpha=0.3)
        plt.xlabel('Frequency (MHz)')
        plt.suptitle('Fraction masked, all beams overlayed')
        plt.savefig('latest2.png')
    
        plt.clf()
        beamfreqs = []
        for i,(b,hh) in enumerate(beam_histories.items()):
            latest = hh[-1]
            beamfreqs.append(latest)
        beamfreqs = np.vstack(beamfreqs)
        nb,nf = beamfreqs.shape
        plt.imshow(beamfreqs, interpolation='nearest', origin='lower',
                   extent=[f_lo,f_hi,0,nb], cmap='hot', aspect='auto')
        plt.xlim(f_lo, f_hi)
        plt.xlabel('Frequency (MHz)')
        plt.ylabel('Beam number')
        plt.suptitle('Fraction masked, by beam x frequency')
        plt.savefig('beamfreq.png')
    
        plt.clf()
        sumimg = np.vstack(sum_history).T
        sh,sw = sumimg.shape
        plt.imshow(sumimg, interpolation='nearest', origin='lower', aspect='auto', cmap='hot',
                   extent=[-0.5, sw-0.5, f_lo, f_hi], vmin=0, vmax=1)
        plt.xlabel('Sample')
        plt.ylabel('Frequency (MHz)')
        #plt.colorbar()
        plt.title('Fraction masked, summed over all beams')
        plt.savefig('summed.png')
    
        plt.clf()
        plt.imshow(beam_map, interpolation='nearest', origin='lower', aspect='auto', cmap='hot', vmin=0, vmax=1)
        #plt.yticks(np.arange(bh))
        plt.ylabel('Beam y')
        plt.xticks(np.arange(bw))
        plt.xlabel('Beam x')
        plt.colorbar()
        plt.title('Fraction masked: Beam map, instantaneous')
        plt.savefig('beam-map.png')
    
        plt.clf()
        sumimg = np.vstack(map_history).T
        sh,sw = sumimg.shape
        plt.imshow(sumimg, interpolation='nearest', origin='lower', aspect='auto', cmap='hot')
        #vmin=0, vmax=1)
        #plt.yticks(np.arange(sh))
        plt.ylabel('Beam number')
        plt.xlabel('Sample')
        plt.colorbar()
        plt.title('Fraction masked, beam map, over time')
        plt.savefig('beam-hist.png')


'''

