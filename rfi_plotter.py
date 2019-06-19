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

def beam_ns(beam):
    return beam % 1000

def beam_ew(beam):
    return beam // 1000

def beam_freq_movie(db, args, nb, nf, beamnum_map, beam_lo, beam_hi, f_lo, f_hi,
                    make_map=False, where=None):
    '''
    nb: number of beams
    nf: number of frequencies
    beamnum_map: dict from beam number to, eg, N/S position
    where: where in the RFI chain to plot
    '''
    print('Beam vs freq movie...')
    dates = []

    whereclause = ''
    #andwhereclause = ''
    if where is not None:
        whereclause = " WHERE where_rfi='%s'" % where
        #andwhereclause = " AND where_rfi='%s'" % where

    #for row in db.execute('SELECT DISTINCT date FROM rfi_meta'):
    print('Query:', 'SELECT DISTINCT date FROM rfi_sum' + whereclause)
    for row in db.execute('SELECT DISTINCT date FROM rfi_sum' + whereclause):
        (date,) = row
        dates.append(date)
    dates.sort()
    print('Found', len(dates), 'distinct dates')

    nbeams = 1 + beam_hi - beam_lo

    bf_sum = np.zeros((nbeams, nf), np.float32)
    bf_n   = np.zeros(nbeams)

    beams = beamnum_map.keys()
    bx = [beam_ew(b) for b in beams]
    by = [beam_ns(b) for b in beams]
    xlo,xhi = min(bx), max(bx)
    ylo,yhi = min(by), max(by)
    beam_map = np.zeros((1+yhi-ylo, 1+xhi-xlo))

    for idate,date in enumerate(dates):
        bf_sum[:,:] = 0
        bf_n[:] = 0
        beam_map[:,:] = 0

        print('Beam-freq movie frame', idate+1, 'of', len(dates), date)

        #sel = '*'
        sel = 'beam, nsamples, nsamples_masked, nt, freqs'
        if where is not None:
            q = 'SELECT ' + sel + ' FROM rfi WHERE date=? AND where_rfi=?'
            qargs = (date, where)
        else:
            q = 'SELECT ' + sel + ' FROM rfi WHERE date=?'
            qargs = (date,)
        print('Query:', q, qargs)
        nrows = 0
        for row in db.execute(q, qargs):
            nrows += 1
            (beam, nsamples, nsamples_masked, nt, blob) = row
            #(d, beam, frame0nano, fpga_start, fpga_end, sample_start, nt, nsamples,
            #nsamples_masked, blob) = row

            if args.column is not None:
                ew = beam_ew(beam)
                if ew != args.column:
                    # skip it!
                    continue

            # NOTE, these frequencies are not flipped (they're in 400..800 order)
            freqs = np.frombuffer(blob, dtype='<i4')

            beamnum = beamnum_map[beam]
            ibeam = beamnum - beam_lo
            bf_sum[ibeam,:] += freqs
            bf_n  [ibeam] += nt

            bx = beam_ew(beam)
            by = beam_ns(beam)
            beam_map[by-ylo, bx-xlo] = float(nsamples_masked) / float(nsamples)

        print('Rows:', nrows)
        from collections import Counter
        print('bf_n values:', Counter(bf_n))
        bf = bf_sum / np.maximum(bf_n[:,np.newaxis], 1)

        plt.clf()
        plt.imshow(bf, interpolation='nearest', origin='lower',
                   extent=[f_lo,f_hi,beam_lo,beam_hi], cmap='hot', aspect='auto')
        plt.xlim(f_lo, f_hi)
        plt.xlabel('Frequency (MHz)')
        if args.avg:
            plt.ylabel('N-S Beam')
        elif args.column is not None:
            plt.ylabel('Beam number')
        else:
            plt.ylabel('Beam')
        wherestr = ''
        if where is not None:
            wherestr = ' ' + where
        tt = 'Fraction masked%s, by beam x freq: %s' % (wherestr, date)
        plt.suptitle(tt)
        plt.colorbar()
        plt.savefig(args.beamfreq_prefix + '-%04i.png' % idate)

        if make_map:
            plt.clf()
            plt.imshow(beam_map, interpolation='nearest', origin='lower',
                       extent=[xlo-0.5, xhi+0.5, ylo-0.5, yhi+0.5],
                       cmap='hot', aspect='auto', vmin=0.3, vmax=0.7)
            plt.xticks(np.arange(1+xhi-xlo))
            plt.xlabel('E-W Beam')
            plt.ylabel('N-S Beam')
            plt.suptitle('Fraction masked, by beam: %s' % date)
            plt.savefig('beammap-%04i.png' % idate)

def parse_datestring(d):
    return datetime.datetime.strptime(d, '%Y-%m-%dT%H:%M:%S')

def mjdtodate(mjd):
    jd = mjdtojd(mjd)
    return jdtodate(jd)

def jdtodate(jd):
    unixtime = (jd - 2440587.5) * 86400. # in seconds
    return datetime.datetime.utcfromtimestamp(unixtime)

def mjdtojd(mjd):
    return mjd + 2400000.5

def jdtomjd(jd):
    return jd - 2400000.5

def datetomjd(d):
    d0 = datetime.datetime(1858, 11, 17, 0, 0, 0)
    dt = d - d0
    # dt is a timedelta object.
    return timedeltatodays(dt)

def datetojd(d):
    return mjdtojd(datetomjd(d))

def timedeltatodays(dt):
    return dt.days + (dt.seconds + dt.microseconds/1e6)/86400.

def freq_vs_time(db, where, date_start, date_end, fn, f_lo, f_hi,
                 period=60.):
    freqtime = []
    dates = []
    datestrings = []

    q = ('SELECT date, freqs, nt_total FROM rfi_sum '
         'WHERE date BETWEEN ? AND ?')
    qargs = [date_start, date_end]
    if where is not None:
        q += ' AND where_rfi=?'
        qargs.append(where)
    q += ' ORDER BY date'

    date_lo, date_hi = [mdates.date2num(parse_datestring(d))
                        for d in [date_start, date_end]]
    print('Date range', date_lo, date_hi)

    timebins = int((date_hi - date_lo) * 86400 / period)
    print(timebins, 'time bins of period', period, 'seconds')

    for row in db.execute(q, qargs):
        (date,blob,nt) = row
        freqs = np.frombuffer(blob, dtype='<i8')
        freqtime.append(100. * freqs / nt)
        dates.append(parse_datestring(date))
        datestrings.append(date)

    print('Got', len(dates), 'rows')

    import fitsio
    F = fitsio.FITS('freq-time.fits', 'rw', clobber=True)
    F.write([np.array([datetomjd(d) for d in dates]),
             np.array(datestrings), np.vstack(freqtime), ],
            names=['mjd', 'datestring', 'freq_masked'])
    F.close()

    nfreq = len(freqtime[0])
    freqs = np.zeros((nfreq, timebins), np.float32)
    nadded = np.zeros(timebins, int)

    for date,freq in zip(dates, freqtime):
        bin = int((mdates.date2num(date) - date_lo) * 86400 / period)
        if bin < 0 or bin >= timebins:
            continue
        freqs[:, bin] += freq
        nadded[bin] += 1
    freqs /= np.maximum(nadded, 1)[np.newaxis, :]
    
    plt.clf()
    plt.imshow(freqs, interpolation='nearest', origin='lower', aspect='auto',
               cmap='hot', extent=[date_lo, date_hi, f_lo, f_hi])
    plt.colorbar()
    ax = plt.gca()
    ax.xaxis_date()
    ax.xaxis.set_major_formatter(mdates.DateFormatter('%m/%d %H:%M'))
    fig = plt.gcf()
    fig.autofmt_xdate()
    plt.ylabel('Frequency (MHz)')
    plt.title('RFI masked percentage: Frequency vs Time')
    fn = 'freq-time.png'
    plt.savefig(fn)
    print('Saved', fn)

def beam_vs_time(db, where, date_start, date_end, f_lo, f_hi,
                 fn_avg, fn_all, beam_map_avg, beam_map_all, period=60.):
    date_lo, date_hi = [mdates.date2num(parse_datestring(d))
                        for d in [date_start, date_end]]
    print('Date range', date_lo, date_hi)
    timebins = int((date_hi - date_lo) * 86400 / period)
    print(timebins, 'time bins of period', period, 'seconds')

    q = ('SELECT date, beam, 100.*nsamples_masked/nsamples FROM rfi_meta '
         'WHERE date BETWEEN ? AND ?')
    qargs = [date_start, date_end]
    if where is not None:
        q += ' AND where_rfi=?'
        qargs.append(where)
    q += ' ORDER BY date,beam'

    lastdate = None
    beamfrac = None
    dates = []
    beamdates = []

    nrows = 0
    printrow = 1000
    for row in db.execute(q, qargs):
        nrows += 1
        if nrows == printrow:
            print('Row', nrows)
            printrow *= 2
        (date, beam, fmasked) = row
        if date != lastdate:
            if beamfrac is not None:
                beamdates.append(beamfrac)
                dates.append(lastdate)
            beamfrac = {}
            lastdate = date
        beamfrac[beam] = fmasked
    if beamfrac is not None:
        beamdates.append(beamfrac)
        dates.append(lastdate)
    allbeams = set()
    for beamfrac in beamdates:
        allbeams.update(beamfrac.keys())
    print('Got', len(dates), 'dates x', len(allbeams), 'total beams')
    print('All beams:', allbeams)

    for avg,fn,beamnum_map in [(False, fn_all, beam_map_all),
                               (True,  fn_avg, beam_map_avg)]:
        beam_lo = min(beamnum_map.values())
        beam_hi = max(beamnum_map.values())
        nbeams = 1 + beam_hi - beam_lo

        beam_times = np.zeros((nbeams, timebins), np.float32)
        beam_times_n = np.zeros((nbeams, timebins), np.uint8)

        beam_dense = np.zeros((nbeams, len(dates)), np.float32)
        beam_dense_n = np.zeros((nbeams, len(dates)), np.uint8)

        mjds = []
        for idate,(date,beamfrac) in enumerate(zip(dates, beamdates)):
            dobj = parse_datestring(date)
            mjds.append(datetomjd(dobj))
            bin = int((mdates.date2num(dobj) - date_lo) * 86400 / period)
            if bin < 0 or bin >= timebins:
                continue
            for beam,f in beamfrac.items():
                beamnum = beamnum_map[beam]
                ibeam = beamnum - beam_lo
                beam_times[ibeam, bin] += f
                beam_times_n[ibeam, bin] += 1

                beam_dense[ibeam, idate] += f
                beam_dense_n[ibeam, idate] += 1

        beam_times /= np.maximum(1, beam_times_n)

        beam_dense /= np.maximum(1, beam_dense_n)

        import fitsio
        fitsfn = 'beam-time-%s.fits' % ('avg' if avg else 'all')
        F = fitsio.FITS(fitsfn, 'rw', clobber=True)
        F.write([np.array(mjds), np.array(dates), beam_dense.T],
                names=['mjd', 'datestring', 'beam_masked'])
        F.close()
        print('Wrote',fitsfn)

        #dateobjs = [datetime.datetime.strptime(iso, '%Y-%m-%dT%H:%M:%S')
        #            for iso in dates]
        # matplotlib numerical dates
        #ndates = [mdates.date2num(d) for d in dateobjs]
    
        plt.clf()
        plt.imshow(beam_times, interpolation='nearest', origin='lower', cmap='hot', aspect='auto',
                   extent=[date_lo, date_hi, beam_lo, beam_hi])
        plt.colorbar()
        ax = plt.gca()
        ax.xaxis_date()
        ax.xaxis.set_major_formatter(mdates.DateFormatter('%m/%d %H:%M'))
        fig = plt.gcf()
        fig.autofmt_xdate()
        plt.xlabel('Date (UTC)')
        if avg:
            plt.ylabel('N-S Beam')
        else:
            plt.ylabel('Beam number')
        plt.title('RFI masked percentage, by beam')
        plt.savefig(fn)
        print('Saved', fn)


def main():

    now = datetime.datetime.utcnow().isoformat()[:19]

    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('rfi_db', nargs=1,
                        help='rfi database filename')
    parser.add_argument('--avg', action='store_true', default=False,
                        help='Average over rows of E-W beams?')
    parser.add_argument('--column', type=int, default=None,
                        help='Select a single E-W column of beams')
    parser.add_argument('--beamfreq-prefix', default='beamfreq',
                        help='Filename prefix for beam-frequency plots')
    parser.add_argument('--where', default='after_rfi',
                        help='Where in the RFI chain to plot ("before_rfi", "after_rfi", "none")')
    parser.add_argument('--start-date', help='Start date, in UTC, default 24 hours before --end-date')
    parser.add_argument('--end-date', default=now, help='End date, in UTC, default "%s"' % now)
    args = parser.parse_args()

    f_lo, f_hi = 400, 800
    
    dbfn = args.rfi_db[0]
    #conn = sqlite3.connect('/data/frb-archiver/dstn/rfi-monitor.db')
    # Read-only:
    #dbfn = '/data/frb-archiver/dstn/rfi-monitor.db'
    #conn = sqlite3.connect('file:'+dbfn + '?mode=ro', uri=True)
    #dbfn = '/data/frb-archiver/dstn/rfi-monitor-2019-01-26T15-51-40.db'
    #conn = sqlite3.connect(dbfn)

    timeout = 60

    uri = 'file:' + dbfn + '?mode=ro'
    conn = sqlite3.connect(uri, timeout, uri=True)
    db = conn.cursor()

    where = args.where
    if where.lower() == 'none':
        where = None

    # HACK -- hard-coded number of beams and frequencies, and beam numbering scheme.
    nb = 1024
    nf = 1024
    allbeams = (list(range(256)) +
                list(range(1000, 1256)) + 
                list(range(2000, 2256)) + 
                list(range(3000, 3256)))

    # Place them in order
    beam_map_all = dict([(v, i) for i,v in enumerate(allbeams)])
    beam_map_avg = dict([(b, beam_ns(b)) for b in allbeams])

    if False:
        if args.avg:
            beamnum_map = dict([(b, beam_ns(b)) for b in allbeams])
        elif args.column is not None:
            beamnum_map = dict([(b, b) for b in allbeams
                                if beam_ew(b) == args.column])
        else:
            # Place them in order
            beamnum_map = dict([(v, i) for i,v in enumerate(allbeams)])
        beam_lo = min(beamnum_map.values())
        beam_hi = max(beamnum_map.values())
        print('Beams:', len(beamnum_map.keys()), 'from', beam_lo, 'to', beam_hi)
        beam_freq_movie(db, args, nb, nf, beamnum_map, beam_lo, beam_hi, f_lo, f_hi,
                        make_map=False, where=where)

    plt.figure(figsize=(10,8))

    date_end = args.end_date
    if args.start_date is None:
        date_start = (parse_datestring(date_end) - datetime.timedelta(1)).isoformat()[:19]
    else:
        date_start = args.start_date

    print('Parsing start date')
    print(parse_datestring(date_start))
    print('Parsing end date')
    print(parse_datestring(date_end))


    # Beam vs Time
    beam_vs_time(db, where, date_start, date_end, f_lo, f_hi,
                 'beam-time-avg.png', 'beam-time-all.png',
                 beam_map_avg, beam_map_all)
    return
    
    # Frequency vs Time
    freq_vs_time(db, where, date_start, date_end, 'freq-time.png', f_lo, f_hi)
    

    
    # Beam vs Freq, averaged over whole day

    dates = []

    q = 'SELECT DISTINCT date FROM rfi_sum WHERE date > ?'
    qargs = [dayago]
    if where is not None:
        q += ' AND where_rfi=?'
        qargs.append(where)
    print('Query:', q, qargs)
    for row in db.execute(q, qargs):
        (date,) = row
        dates.append(date)
    dates.sort()
    print('Found', len(dates), 'distinct dates')

    # re-use the row-averaged values from above
    nbeams = 1 + beam_hi - beam_lo
    bf_sum = np.zeros((nbeams, nf), np.float32)
    bf_n   = np.zeros(nbeams, np.int64)
    #beams = beamnum_map.keys()
    #bx = [beam_ew(b) for b in beams]
    #by = [beam_ns(b) for b in beams]
    #xlo,xhi = min(bx), max(bx)
    #ylo,yhi = min(by), max(by)
    #beam_map = np.zeros((1+yhi-ylo, 1+xhi-xlo))

    for idate,date in enumerate(dates):
        #bf_sum[:,:] = 0
        #bf_n[:] = 0
        #beam_map[:,:] = 0
        print('Beam-freq for sample', idate+1, 'of', len(dates))
        sel = 'beam, nsamples, nsamples_masked, nt, freqs'
        if where is not None:
            q = 'SELECT ' + sel + ' FROM rfi WHERE date=? AND where_rfi=?'
            qargs = (date, where)
        else:
            q = 'SELECT ' + sel + ' FROM rfi WHERE date=?'
            qargs = (date,)
        print('Query:', q, qargs)
        nrows = 0
        for row in db.execute(q, qargs):
            nrows += 1
            (beam, nsamples, nsamples_masked, nt, blob) = row
            # NOTE, these frequencies are not flipped (they're in 400..800 order)
            freqs = np.frombuffer(blob, dtype='<i4')
            beamnum = beamnum_map[beam]
            ibeam = beamnum - beam_lo
            bf_sum[ibeam,:] += freqs
            bf_n  [ibeam] += nt
            #bx = beam_ew(beam)
            #by = beam_ns(beam)
            #beam_map[by-ylo, bx-xlo] = float(nsamples_masked) / float(nsamples)

    print('Max bf_n:', np.max(bf_n))
    bf = bf_sum / np.maximum(bf_n[:,np.newaxis], 1)

    plt.clf()
    plt.imshow(bf, interpolation='nearest', origin='lower',
               extent=[f_lo,f_hi,beam_lo,beam_hi], cmap='hot', aspect='auto')
    plt.xlim(f_lo, f_hi)
    plt.xlabel('Frequency (MHz)')
    plt.ylabel('N-S Beam')
    wherestr = ''
    if where is not None:
        wherestr = ' ' + where
    tt = 'Fraction masked%s, by beam x freq: %s' % (wherestr, date)
    plt.suptitle(tt)
    plt.colorbar()
    fn = 'beam-freq-avg.png'
    plt.savefig(fn)
    print('Saved', fn)

    return
    
    # Latest sample: beam vs freq
    
    db.execute('SELECT max(date) from rfi_meta')
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
    
    if args.avg:
        beams = [r.beam for r in allbeams]
        bx = [beam_ew(b) for b in beams]
        by = [beam_ns(b) for b in beams]
        xlo,xhi = min(bx), max(bx)
        ylo,yhi = min(by), max(by)
        beam_map = np.zeros((1+yhi-ylo, 1+xhi-xlo))
        for r,x,y in zip(allbeams, bx, by):
            beam_map[y-ylo, x-xlo] = float(r.nsamples_masked) / float(r.nsamples)
    
        plt.clf()
        plt.imshow(beam_map, interpolation='nearest', origin='lower', aspect='auto', cmap='hot',
                   extent=[xlo, xhi, ylo, yhi])
        plt.ylabel('Beam N-S')
        plt.xlabel('Beam E-W')
        plt.colorbar()
        plt.title('Fraction masked: Beam map, instantaneous')
        plt.savefig('beam-map.png')
    
    
    plt.clf()
    beamfreqs = []
    for r in allbeams:
        beamfreqs.append(r.freqs_masked)
    beamfreqs = np.vstack(beamfreqs)
    
    if args.avg:
        nb,nf = beamfreqs.shape
        bf_sum = np.zeros((nbeams, nf), np.float32)
        bf_n   = np.zeros(nbeams)
        for i,r in enumerate(allbeams):
            beamnum = beamnum_map[r.beam]
            ibeam = beamnum - beam_lo
            bf_sum[ibeam,:] += beamfreqs[i,:]
            bf_n  [ibeam] += 1
        beamfreqs = bf_sum / np.maximum(1, bf_n[:,np.newaxis])
    
    nb,nf = beamfreqs.shape
    plt.imshow(beamfreqs, interpolation='nearest', origin='lower',
               extent=[f_lo,f_hi,beam_lo,beam_hi], cmap='hot', aspect='auto')
    plt.xlim(f_lo, f_hi)
    plt.xlabel('Frequency (MHz)')
    if args.avg:
        plt.ylabel('N-S Beam')
    else:
        plt.ylabel('Beam number')
    plt.suptitle('Fraction masked, by beam x frequency')
    plt.savefig('beamfreq.png')

    if not args.avg:
        return

if __name__ == '__main__':
    main()

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

