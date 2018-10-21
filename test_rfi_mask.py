from __future__ import print_function
import matplotlib
matplotlib.use('Agg')
import pylab as plt

import simulate_l0
import numpy as np
from rpc_client import read_msgpack_file, RpcClient, InjectData
from time import sleep
import subprocess
import os
from glob import glob

print('Deleting existing msgpack files...')
fns = glob('chunk-test-rfi-mask-*.msgpack')
for fn in fns:
    print('  ', fn)
    os.remove(fn)

l0 = simulate_l0.l0sim('l0_configs/l0_test_rfi.yml', 1.0)
client = RpcClient({'a':'tcp://127.0.0.1:5555'})

if True:
    l1cmd = './ch-frb-l1 -fv l1_configs/l1_test_rfi.yml rfi_configs/rfi_testing.json bonsai_production_noups_nbeta1_v2.hdf5 xxx'
    need_rfi = True
else:
    l1cmd = './ch-frb-l1 -fv l1_configs/l1_test_norfi.yml rfi_configs/rfi_testing_no.json bonsai_production_noups_nbeta1_v2.hdf5 xxx'
    need_rfi = False

l1 = subprocess.Popen(l1cmd, shell=True)
# wait until L1 is ready to receive
sleep(5)

beam_id = 0
fpga_counts_per_sample = 384
nt = 1024
nf = 16384
nrfi = 1024
nt_coarse = nt // 16
nf_coarse = 1024

chunk0 = 50

offset = np.empty((nf_coarse, nt_coarse), np.float32)
scale = np.empty((nf_coarse, nt_coarse), np.float32)
rfi = None

offset[:,:] = -64.
scale[:,:] = 1.

prom_cmd = 'wget http://127.0.0.1:9999/metrics -O -'

writereq = None

if True:
    # inject some data
    beam = beam_id
    fpga0 = 52 * 1024 * 384
    nfreq = nf
    sample_offsets = np.zeros(nfreq, np.int32)
    data = []
    for f in range(nfreq):
        sample_offsets[f] = int(0.2 * f)
        data.append(1000. * np.ones(200, np.float32))
    print('Injecting data spanning', np.min(sample_offsets), 'to', np.max(sample_offsets), ' samples')
    inj = InjectData(beam, 0, fpga0, sample_offsets, data)
    R = client.inject_data(inj, wait=True)
    print('Results:', R)

for i in range(20):
    data = np.clip(128. + 20. * np.random.normal(size=(nf, nt)), 1, 254).astype(np.uint8)
    # Make an RFI spike
    #data[:, i*50:i*50+100] = 200
    # in the frequency direction (where we have better counting)
    data[i*50:i*50+100, :] = 200
    ichunk = i + chunk0
    
    ch = simulate_l0.assembled_chunk(beam_id, fpga_counts_per_sample, ichunk,
                                     data, scale, offset, rfi)
    #print('Chunk:', ch)
    print('Sending chunk...')
    l0.send_chunk(0, ch)

    if i == 5:
        os.system(prom_cmd)

        print('After sending 5 chunks:')
        print(client.get_statistics())

        print('Sending write request...')
        res = client.write_chunks([0], 0, (50 + 4) * 384 * 1024,
                                  'chunk-test-rfi-mask-(FPGA0).msgpack', need_rfi=need_rfi,
                                  waitAll=False)
        print('Got write request result:', res)
        reqs,token = res
        writereq = reqs[0]
        for c in writereq:
            print('  ', c)
        sleep(1)


    if writereq is not None:
        for c in writereq:
            print('Status of', c.filename, ':', client.get_writechunk_status(c.filename))

    # Give L1 some time to process...
    #sleep(2)
    sleep(10)

os.system(prom_cmd)

l1.terminate()

# Check contents of msgpack files.
fns = glob('chunk-test-rfi-mask-*.msgpack')
assert(len(fns) == 4)
fns.sort()
for i,fn in enumerate(fns):
    chunk = read_msgpack_file(fn)
    print('Chunk: ', chunk)

    assert(chunk.rfi_mask is not None)
    nf,nt = chunk.rfi_mask.shape
    sample0 = chunk.fpga0 / chunk.fpga_counts_per_sample

    plt.clf()
    plt.imshow(chunk.rfi_mask, interpolation='nearest', origin='lower',
               vmin=0, vmax=1, cmap='gray',
               extent=[sample0, sample0+nt * chunk.binning, 0, nf])
    plt.xlabel('sample number (~ms)')
    plt.ylabel('(downsampled) frequency')
    fn = 'chunk-%i.png' % i
    plt.savefig(fn)
    print('Wrote', fn)
