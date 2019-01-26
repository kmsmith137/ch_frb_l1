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


#conn = sqlite3.connect('/data/frb-archiver/dstn/rfi-monitor.db')
# Read-only:
#dbfn = '/data/frb-archiver/dstn/rfi-monitor.db'
dbfn = '/tmp/rfi-monitor-tst.db'
conn = sqlite3.connect(dbfn)
#conn = sqlite3.connect('file:'+dbfn + '?mode=ro', uri=True)


db = conn.cursor()

# dates = []
# for row in db.execute('SELECT DISTINCT date from rfi'):
#     (d,) = row
#     dates.append(d)
# print('Dates:', dates[:10])

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

sys.exit(0)

# Beam vs Time

lastdate = None
beamfrac = None
dates = []
beamdates = []

for row in db.execute('SELECT date, beam, 100.*nsamples_masked/nsamples FROM rfi '
                      'ORDER BY date, beam'):
    (date, beam, fmasked) = row
    if date != lastdate:
        if beamfrac is not None:
            beamdates.append(beamfrac)
            dates.append(lastdate)
        beamfrac = {}
        lastdate = date
    # UNfortunately, "nsamples_masked" is actually "UNmasked"
    beamfrac[beam] = 100. - fmasked
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
#locs,labels = plt.xticks()
#print('x ticks:', locs)
#datelabels = [dates[int(i)] for i in locs]
#plt.xticks(locs, datelabels)
#plt.xlabel('Sample number (time)')
plt.xlabel('Date (UTC)')
plt.ylabel('Beam')
plt.title('RFI masked percentage, by beam')
plt.savefig('beam-time.png')

sys.exit(0)


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


