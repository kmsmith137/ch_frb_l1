from __future__ import print_function
import matplotlib
matplotlib.use('Agg')
import pylab as plt
import numpy as np
from rpc_client import RpcClient
from time import sleep
import os
from glob import glob
import time
import datetime

import sqlite3

date = datetime.datetime.utcnow().isoformat()[:19]
date = date.replace(':', '-')

dbfn = '/data/frb-archiver/dstn/rfi-monitor-' + date + '.db'

create = not(os.path.exists(dbfn))
conn = sqlite3.connect(dbfn)
db = conn.cursor()

if create:
    db.execute('''CREATE TABLE rfi
                 (date text, beam int, frame0nano int, fpga_start int, fpga_end int, sample_start int,
                  nt int, nsamples int, nsamples_masked int, freqs blob)''')
    # Like "rfi" but without the "blob" -- makes for much faster scans for beam vs time plots.
    db.execute('''CREATE TABLE rfi_meta
                 (date text, beam int, frame0nano int, fpga_start int, fpga_end int, sample_start int,
                  nt int, nsamples int, nsamples_masked int)''')
    # sum over all beams
    db.execute('''CREATE TABLE rfi_sum
                 (date text, nbeams int, frame0nano int, fpga_start int, fpga_end int, sample_start int,
                  nt int, nsamples int, nsamples_masked int, nt_total, freqs blob)''')
    conn.commit()



rpc_servers = [
    "tcp://10.6.201.10:5555",
    "tcp://10.7.201.10:5555",
    "tcp://10.6.201.11:5555",
    "tcp://10.7.201.11:5555",
    "tcp://10.6.201.12:5555",
    "tcp://10.7.201.12:5555",
    "tcp://10.6.201.13:5555",
    "tcp://10.7.201.13:5555",
    "tcp://10.6.201.14:5555",
    "tcp://10.7.201.14:5555",
    "tcp://10.6.201.15:5555",
    "tcp://10.7.201.15:5555",
    "tcp://10.6.201.16:5555",
    "tcp://10.7.201.16:5555",
    "tcp://10.6.201.17:5555",
    "tcp://10.7.201.17:5555",
    "tcp://10.6.201.18:5555",
    "tcp://10.7.201.18:5555",
    "tcp://10.6.201.19:5555",
    "tcp://10.7.201.19:5555",
]
#'tcp://cf1n0:5555']

client = RpcClient(dict([(s,s) for s in rpc_servers]), debug=False)
timeout = 1000

# int
period_seconds = 60

fpga_counts_per_sample = None

frame0_nano = None

fpga_start = None
fpga_last = None
t_last = None

beam_histories = {}
sum_history = []
map_history = []

while True:
    if fpga_counts_per_sample is None:
        print('Sending get_stats()')
        R = client.get_statistics(timeout=timeout)
        for stats,node in zip(R, rpc_servers):
            # keys = list(stats[0].keys())
            # keys.sort()
            # for k in keys:
            #     print('  ', k, '=', stats[0][k])

            if stats is not None:
                key = b'fpga_counts_per_sample'
                fpga_counts_per_sample = stats[0][key]
                print('Got fpga counts per sample = ', fpga_counts_per_sample)

                key = b'frame0_nano'
                frame0_nano = stats[0][key]
                print('Got frame0_nano', frame0_nano)

                break



    # If we wanted to dead-reckon the time between requests, we could avoid this call...
    #fpga_start = None
    if fpga_start is None:
        R = client.get_max_fpga_counts(timeout=timeout)
        #print('Max fpga:', S)
        for fpgas,node in zip(R, rpc_servers):
            where = b'after_rfi'
            if fpgas is None:
                print('No get_max_fpga_counts from', node)
                continue
            for wh,beam,fpga in fpgas:
                if wh == where and fpga is not None and fpga > 0:
                    fpga_start = fpga
                    time_start = time.time()
                    print('Found FPGA start:', fpga_start, ' for beam', beam, 'on node', node)
                    break

    if fpga_counts_per_sample is None or fpga_start is None:
        sleep(5)
        continue

    fpga_minute = fpga_counts_per_sample * 1024 * period_seconds
    t_minute = fpga_minute * 2.56e-6

    if fpga_last is None:
        fpga_min = ((fpga_start // fpga_minute) - 2) * fpga_minute
    else:
        fpga_min = fpga_last

    fpga_max = fpga_min + fpga_minute

    # next time around...
    fpga_last = fpga_max

    print('Requesting RFI mask for FPGA range', fpga_min, 'to', fpga_max)
    R = client.get_summed_masked_frequencies(fpga_min, fpga_max)
    #print('Got', R)
    allbeams = []
    for r in R:
        if r is not None:
            allbeams.extend(r)

    print('Got results for', len(allbeams), 'beams')

    date = datetime.datetime.utcnow().isoformat()[:19]

    sumhist = 0
    sumt = 0
    bignsamples = 0
    bignmasked = 0

    r0 = None
    for r in allbeams:
        # Frequency ordering flip!
        f = np.flip(r.freqs_masked, 0)
        sumhist = sumhist + f
        sumt += r.nt

        bignsamples += r.nsamples
        bignmasked += r.nsamples_masked

        blob = f.astype('<i4').tobytes()

        db.execute('INSERT INTO rfi VALUES (?,?,?,?,?,?,?,?,?,?)',
                   (date, r.beam, frame0_nano, r.fpga_start, r.fpga_end, r.pos_start, r.nt,
                    r.nsamples, r.nsamples_masked, blob))
        # (date text, beam int, frame0nano int, fpga_start int, fpga_end int, sample_start int,
        #  nt int, nsamples int, nsamples_masked int, freqs blob)

        db.execute('INSERT INTO rfi_meta VALUES (?,?,?,?,?,?,?,?,?)',
                   (date, r.beam, frame0_nano, r.fpga_start, r.fpga_end, r.pos_start, r.nt,
                    r.nsamples, r.nsamples_masked))

        if r0 is None:
            r0 = r
        else:
            if r.fpga_start != r0.fpga_start:
                print('Warning: mismatch fpga_start!')

    if len(allbeams):
        # sum over all beams
        blob = sumhist.astype('<i8').tobytes()
        db.execute('INSERT INTO rfi_sum VALUES (?,?,?,?,?,?,?,?,?,?,?)',
                   (date, len(allbeams), frame0_nano, r0.fpga_start, r0.fpga_end, r0.pos_start,
                    r0.nt, bignsamples, bignmasked, sumt, blob))
        # (date text, nbeams int, frame0nano int, fpga_start int, sample_start int,
        #  nt int, nsamples int, nsamples_masked int, nt_total int, freqs blob)''')

    conn.commit()
                 
    if t_last is None:
        t_last = time.time()
    t_now = time.time()
    t_sleep = t_last + t_minute - t_now
    t_last += t_minute
    print('Sleeping for', t_sleep)
    if t_sleep < 0:
        print('We have fallen behind!')
        sys.exit(-1)

    sleep(t_sleep)

