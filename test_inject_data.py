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

def bin_image(data, S):
    # rebin image data
    H,W = data.shape
    sH,sW = (H+S-1)//S, (W+S-1)//S
    newdata = np.zeros((sH,sW), dtype=data.dtype)
    for i in range(S):
        for j in range(S):
            subdata = data[i::S, j::S]
            subh,subw = subdata.shape
            newdata[:subh,:subw] += subdata
    return newdata

def main():
    print('Deleting existing msgpack files...')
    fns = glob('chunk-injected-*.msgpack')
    for fn in fns:
        print('  ', fn)
        os.remove(fn)
    
    l0 = simulate_l0.l0sim('l0_configs/l0_test_rfi.yml', 1.0)
    client = RpcClient({'a':'tcp://127.0.0.1:5555'})
    
    l1cmd = './ch-frb-l1 -fv l1_configs/l1_test_norfi.yml rfi_configs/rfi_testing_inject.json bonsai_production_noups_nbeta1_v2.hdf5 xxx'
    need_rfi = False
    
    l1 = subprocess.Popen(l1cmd, shell=True)
    # wait until L1 is ready to receive
    sleep(5)

    success = True
    try:
        run_main(l0, client)
    except:
        import traceback
        traceback.print_exc()
        success = False
    print('Killing L1 process...')
    l1.terminate()
    sleep(1)
    if l1.poll() is None:
        l1.kill()
    if not success:
        return

    # Check contents of msgpack files.
    fns = glob('chunk-injected-000000*.msgpack')
    fns.sort()

    nchunks = ichunk - chunk0
    print('Keeping only', nchunks, 'chunks that we sent data for')
    fns = fns[:nchunks]

    binned = []
    B = 16
    bsample0 = 0

    for i,fn in enumerate(fns):
        print('Reading', fn)
        chunk = read_msgpack_file(fn)
        print('Chunk: ', chunk)
    
        sample0 = chunk.fpga0 / chunk.fpga_counts_per_sample
        I,W = chunk.decode()
        nf,nt = I.shape

        b = bin_image(I, B)
        binned.append(b)
        if i == 0:
            bsample0 = sample0

        plt.clf()
        plt.imshow(I, interpolation='nearest', origin='lower',
                   #vmin=0, vmax=1, cmap='gray',
                   extent=[sample0, sample0+nt * chunk.binning, 0, nf], aspect='auto')
        plt.xlabel('sample number (~ms)')
        plt.ylabel('frequency bin')
        plt.title('Intensity')
        fn = 'injected-%02i.png' % i
        plt.savefig(fn)
        print('Wrote', fn)

    binned = np.hstack(binned)
    bh,bw = binned.shape
    plt.clf()
    plt.imshow(binned, interpolation='nearest', origin='lower',
               extent=[bsample0, bsample0+bw*B, 0, bh*B], aspect='auto')
    plt.xlabel('sample number (~ms)')
    plt.ylabel('frequency bin')
    plt.title('Injected')
    fn = 'injected.png'
    plt.savefig(fn)
    print('Wrote', fn)

        
def run_main(l0, client):
    beam_id = 0
    fpga_counts_per_sample = 384
    nt = 1024
    nf = 16384
    nrfi = 1024
    nt_coarse = nt // 16
    nf_coarse = 1024
    
    chunk0 = 50
    ichunk = chunk0
    
    offset = np.empty((nf_coarse, nt_coarse), np.float32)
    scale = np.empty((nf_coarse, nt_coarse), np.float32)
    rfi = None
    
    offset[:,:] = -64.
    scale[:,:] = 1.
    
    # Create data to be injected.
    beam = beam_id
    nfreq = nf
    sample_offsets = np.zeros(nfreq, np.int32)
    data = []
    for f in range(nfreq):
        sample_offsets[f] = int(0.2 * f)
        data.append(200. * np.ones(200, np.float32))
    print('Injecting data spanning', np.min(sample_offsets), 'to', np.max(sample_offsets), ' samples')

    # try injecting data before the L1 node has received its first packet
    fpga0 = 0

    inj = InjectData(beam, 0, fpga0, sample_offsets, data)
    print('Inject_data (before first packet)')
    R = client.inject_data(inj, wait=True)
    print('Results:', R)
    R = R[0]
    ### ?? should this error out?
    # Error message!
    #assert(len(R) > 0)

    if True:
        # Send a first chunk to set the initial FPGA counts!
        chdata = np.clip(128. + 20. * np.random.normal(size=(nf, nt)),
                         1, 254).astype(np.uint8)
        ch = simulate_l0.assembled_chunk(beam_id, fpga_counts_per_sample, ichunk,
                                         chdata, scale, offset, rfi)
        print()
        print('Sending chunk', ichunk)
        ichunk += 1
        l0.send_chunk(0, ch)
        # Give L1 some time to process...
        sleep(2)

    # try injecting data too late
    inj = InjectData(beam, 0, fpga0, sample_offsets, data)
    print('Inject_data (too late)')
    R = client.inject_data(inj, wait=True)
    print('Results:', R)
    R = R[0]
    # Error message!
    assert(len(R) > 0)

    fpga0 = (chunk0 + 2) * 1024 * 384

    # Try injecting data for wrong beam
    badbeam = 100
    inj = InjectData(badbeam, 0, fpga0, sample_offsets, data)
    print('Inject_data (bad beam)')
    R = client.inject_data(inj, wait=True)
    print('Results:', R)
    R = R[0]
    # Error message!
    assert(len(R) > 0)

    # Try injecting data with wrong number of frequency bins
    inj = InjectData(beam, 0, fpga0, sample_offsets[:-1], data)
    print('Inject_data (bad freqs)')
    R = client.inject_data(inj, wait=True)
    print('Results:', R)
    R = R[0]
    # Error message!
    assert(len(R) > 0)

    # This injection should work!
    inj = InjectData(beam, 0, fpga0, sample_offsets, data)
    print('Inject_data')
    R = client.inject_data(inj, wait=True)
    print('Results:', R)
    R = R[0]
    # Error message!
    assert(len(R) == 0)

    import simpulse
    nt = 1024
    nfreq = nf
    freq_lo = 400.
    freq_hi = 800.
    dm = 500.
    sm = 3. # ms
    width = 0.05 # s
    fluence = 100.
    spectral_index = -1.
    undispersed_t = 0.
    sp = simpulse.single_pulse(nt, nfreq, freq_lo, freq_hi, dm, sm, width, fluence, spectral_index, undispersed_t)
    R2 = client.inject_single_pulse(beam, nfreq, sp, fpga0, wait=True)
    print('Results:', R2)
        
    for i in range(15):
        data = np.clip(128. + 20. * np.random.normal(size=(nf, nt)), 1, 254).astype(np.uint8)
        ch = simulate_l0.assembled_chunk(beam_id, fpga_counts_per_sample, ichunk,
                                         data, scale, offset, rfi)
        print()
        print('Sending chunk', ichunk)
        ichunk += 1
        l0.send_chunk(0, ch)
        # Give L1 some time to process...
        sleep(2)

    print('Ending streams');
    l0.end_streams()
    sleep(5)
    

if __name__ == '__main__':
    main()
