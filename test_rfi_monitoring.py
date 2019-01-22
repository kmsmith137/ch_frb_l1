from __future__ import print_function
import matplotlib
matplotlib.use('Agg')
import pylab as plt

import simulate_l0
import numpy as np
from rpc_client import read_msgpack_file, RpcClient
from time import sleep
import subprocess
import os
from glob import glob
import time

l0 = simulate_l0.l0sim('l0_configs/l0_test_rfi.yml', 1.0)
rpc_servers = ['tcp://127.0.0.1:5555']
#client = RpcClient({'a':'tcp://127.0.0.1:5555'})
client = RpcClient(dict([(s,s) for s in rpc_servers]))

#l1cmd = './ch-frb-l1 -fv l1_configs/l1_test_rfi.yml rfi_configs/rfi_testing.json bonsai_production_noups_nbeta1_v2.hdf5 xxx'
l1cmd = './ch-frb-l1 -frv l1_configs/l1_test_rfi.yml rfi_configs/rfi_testing.json'
need_rfi = True

l1 = subprocess.Popen(l1cmd, shell=True)
# wait until L1 is ready to receive
sleep(5)


#### FIXME -- time to create a test rig for the L1 process, giving us the ability
#### to check on the status of, eg, the rf_pipelines / rfi chain / bonsai dedisper
#### threads, so we can fire in new data at the correct rate.


beam_id = 0
l0_fpga_counts_per_sample = 384
nt = 1024
nf = 16384
nrfi = 1024
nt_coarse = nt // 16
nf_coarse = 1024

chunk0 = 50

offset = np.empty((nf_coarse, nt_coarse), np.float32)
scale = np.empty((nf_coarse, nt_coarse), np.float32)
rfi = None

offset[:,:] = -128.
scale[:,:] = 1.

#fpga_start = None
#time_start = 0
fpga_counts_per_sample = None

for i in range(60):
    data = np.clip(128. + 20. * np.random.normal(size=(nf, nt)), 1, 254).astype(np.uint8)
    # Make an RFI spike
    # in the frequency direction (where we have better counting)
    data[i*50:i*50+100, :] = 200
    ichunk = i + chunk0
    
    ch = simulate_l0.assembled_chunk(beam_id, l0_fpga_counts_per_sample, ichunk,
                                     data, offset, scale, rfi)
    print('Sending chunk...')
    l0.send_chunk(0, ch)

    # Give L1 some time to process...
    sleep(2)

    if fpga_counts_per_sample is None:
        R = client.get_statistics(timeout=5)
        for stats,node in zip(R, rpc_servers):
            if stats is not None:
                key = 'fpga_counts_per_sample'
                fpga_counts_per_sample = stats[0][key]
                print('Got fpga counts per sample = ', fpga_counts_per_sample)
                break

    fpga_start = None
    if fpga_start is None:
        R = client.get_max_fpga_counts(timeout=5)
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
                    print('Found FPGA start:', fpga_start)
                    break

    if fpga_counts_per_sample is not None and fpga_start is not None:
        #fpga_minute = fpga_counts_per_sample * 1024 * 60
        fpga_minute = fpga_counts_per_sample * 1024 * 5

        fpga_min = ((fpga_start // fpga_minute) - 2) * fpga_minute
        fpga_max = ((fpga_start // fpga_minute) - 1) * fpga_minute

        print('Requesting frequencies for FPGA range', fpga_min, 'to', fpga_max)
        R = client.get_summed_masked_frequencies(fpga_min, fpga_max)
        print('Got', R)

