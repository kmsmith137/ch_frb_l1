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

rpc_servers = ['tcp://cf1n0:5555']

client = RpcClient(dict([(s,s) for s in rpc_servers]), debug=False)
timeout = 1000

fpga_counts_per_sample = None

fpga_start = None
fpga_last = None
t_last = None

beam_histories = {}
sum_history = []
map_history = []

while True:
    if fpga_counts_per_sample is None:
        R = client.get_statistics(timeout=timeout)
        for stats,node in zip(R, rpc_servers):
            if stats is not None:
                key = 'fpga_counts_per_sample'
                fpga_counts_per_sample = stats[0][key]
                print('Got fpga counts per sample = ', fpga_counts_per_sample)
                break

    # If we wanted to dead-reckon the time between requests, we could avoid this call...
    #fpga_start = None
    if fpga_start is None:
        R = client.get_max_fpga_counts(timeout=timeout)
        #print('Max fpga:', S)
        for fpgas,node in zip(R, rpc_servers):
            where = 'after_rfi'
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

    #fpga_minute = fpga_counts_per_sample * 1024 * 60
    #fpga_minute = fpga_counts_per_sample * 1024 * 5
    fpga_minute = fpga_counts_per_sample * 1024 * 2

    t_minute = fpga_minute * 2.56e-6
    #print('Time period:', t_minute)

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
    plt.yticks(np.arange(bh))
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
    plt.yticks(np.arange(sh))
    plt.xlabel('Sample')
    plt.ylabel('Beam number')
    plt.colorbar()
    plt.title('Fraction masked, beam map, over time')
    plt.savefig('beam-hist.png')
                 
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

