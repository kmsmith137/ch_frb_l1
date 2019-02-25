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

import simpulse

def bin_image(data, Sy, Sx):
    # rebin image data
    H,W = data.shape
    sH,sW = (H+Sy-1)//Sy, (W+Sx-1)//Sx
    newdata = np.zeros((sH,sW), dtype=data.dtype)
    for i in range(Sy):
        for j in range(Sx):
            subdata = data[i::Sy, j::Sx]
            subh,subw = subdata.shape
            newdata[:subh,:subw] += subdata
    return newdata

def main():
    # chunk0 = 50
    # fpga0 = (chunk0 + 2) * 1024 * 384
    # #fpga0 = 0
    # beam = 0
    # fpga_counts_per_sample = 384
    # 
    # nt = 1024
    # nf = 16384
    # nfreq = nf
    # freq_lo = 400.
    # freq_hi = 800.
    # width = 0.003 # s
    # fluence = 10.
    # spectral_index = -1.
    # 
    # dm = 50.
    # sm = 1. # ms
    # undispersed_t = 12.
    # sp = simpulse.single_pulse(nt, nfreq, freq_lo, freq_hi, dm, sm, width, fluence, spectral_index, undispersed_t)
    # # Reverse frequency ordering!
    # t0,t1 = sp.get_endpoints()
    # dt = 384 * 2.56e-6
    # ntp = int((t1 - t0) / dt)
    # nsparse = sp.get_n_sparse(t0, t1, ntp)
    # print('Pulse time range:', t0, t1, 'NT', ntp, 'N sparse:', nsparse)
    # sparse_data = np.zeros(nsparse, np.float32)
    # sparse_i0 = np.zeros(nfreq, np.int32)
    # sparse_n = np.zeros(nfreq, np.int32)
    # t1x = t0 + ntp * dt
    # print('Using t range', t0, t1x, 'N', ntp)
    # sp.add_to_timestream_sparse(sparse_data, sparse_i0, sparse_n, t0, t1x, ntp, 1.)
    # # convert sparse_data into a list of numpy arrays (one per freq)
    # data = []
    # ntotal = 0
    # for n in sparse_n:
    #     data.append(sparse_data[ntotal:ntotal+n])
    #     ntotal += n
    # del sparse_n
    # del sparse_data
    # # HACK -- reverse frequencies!!
    # sparse_i0 = sparse_i0[::-1]
    # data = list(reversed(data))
    # print('Sparse_i0 range:', sparse_i0.min(), sparse_i0.max())
    # 
    # for j,(i,d) in enumerate(zip(sparse_i0, data)):
    #     print(j, 'i0', i, 'data length', len(d))
    # 
    # #print('Sending fpga0:', fpga0)
    # fpga_offset = int(fpga0 + (t0 / 2.56e-6))
    # print('FPGA_offset:', fpga_offset)
    # injdata = InjectData(beam, 0, fpga_offset, sparse_i0, data)
    # #R2 = client.inject_data(injdata, wait=True)
    # #print('Results:', R2)


    print('Deleting existing msgpack files...')
    fns = glob('chunk-injected-*.msgpack')
    for fn in fns:
        print('  ', fn)
        os.remove(fn)
    
    l0 = simulate_l0.l0sim('l0_configs/l0_test_inject.yml', 1.0)
    client = RpcClient({'a':'tcp://127.0.0.1:5566'})

    l1cmd = './ch-frb-l1 -fv l1_configs/l1_test_inject.yml rfi_configs/rfi_testing_inject.json bonsai_configs/bonsai_test_inject.hdf5 xxx'
    #need_rfi = False
    need_rfi = True

    l1 = subprocess.Popen(l1cmd, shell=True)
    # wait until L1 is ready to receive
    sleep(5)

    success = True
    try:
        nchunks = run_main(l0, client)
    except:
        import traceback
        traceback.print_exc()
        success = False

    # print('Killing L1 process...')
    # l1.terminate()
    # sleep(1)
    # if l1.poll() is None:
    #     l1.kill()
    # if not success:
    #     return

    # Check contents of msgpack files.
    fns = glob('chunk-injected-000000*.msgpack')
    fns.sort()

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

        b = bin_image(I, B, B)
        binned.append(b)
        if i == 0:
            bsample0 = sample0

        print('Has RFI mask:', chunk.has_rfi_mask)
        if chunk.has_rfi_mask:
            print('Chunk shape:', I.shape, 'RFI mask shape', chunk.rfi_mask.shape)
            binf = bin_image(I, 16, 1)
            print('binf:', binf.shape)

            plt.clf()
            plt.subplot(1,2,1)
            plt.imshow(binf, interpolation='nearest', origin='lower')
            plt.colorbar()
            plt.subplot(1,2,2)
            plt.imshow(chunk.rfi_mask, interpolation='nearest', origin='lower')
            plt.colorbar()
            fn = 'mask-%02i.png' % i
            plt.savefig(fn)

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

    # # This injection should work!
    # inj = InjectData(beam, 0, fpga0, sample_offsets, data)
    # print('Inject_data')
    # R = client.inject_data(inj, wait=True)
    # print('Results:', R)
    # R = R[0]
    # # Error message!
    # assert(len(R) == 0)

    #print('Requesting streaming...')
    #client.stream('dstntest', acq_dev='nfs')

    nt = 1024
    nfreq = nf
    freq_lo = 400.
    freq_hi = 800.
    #dm = 500.
    dm = 200.
    sm = 3. # ms
    #width = 0.05 # s
    width = 0.003 # s
    fluence = 10.
    spectral_index = -1.
    undispersed_t = 0.
    #sp = simpulse.single_pulse(nt, nfreq, freq_lo, freq_hi, dm, sm, width, fluence, spectral_index, undispersed_t)
    #R2 = client.inject_single_pulse(beam, sp, fpga0, wait=True, nfreq=nfreq)
    #print('Results:', R2)


    dm = 50.
    sm = 1.
    fluence = 0.1
    undispersed_t = 8.
    sp = simpulse.single_pulse(nt, nfreq, freq_lo, freq_hi, dm, sm, width, fluence, spectral_index, undispersed_t)
    # R2 = client.inject_single_pulse(beam, sp, fpga0, wait=True, nfreq=nfreq)
    # print('Results:', R2)

    #undispersed_t = 12.
    undispersed_t = 0.
    sp = simpulse.single_pulse(nt, nfreq, freq_lo, freq_hi, dm, sm, width, fluence, spectral_index, undispersed_t)
    # Reverse frequency ordering!
    t0,t1 = sp.get_endpoints()
    dt = 384 * 2.56e-6
    ntp = int((t1 - t0) / dt)
    t1x = t0 + ntp * dt
    print('Using t range', t0, t1x, 'N', ntp)
    nsparse = sp.get_n_sparse(t0, t1x, ntp)
    print('Pulse time range:', t0, t1x, 'NT', ntp, 'N sparse:', nsparse)
    sparse_data = np.zeros(nsparse, np.float32)
    sparse_i0 = np.zeros(nfreq, np.int32)
    sparse_n = np.zeros(nfreq, np.int32)
    sp.add_to_timestream_sparse(sparse_data, sparse_i0, sparse_n, t0, t1x, ntp, 1.)
    # convert sparse_data into a list of numpy arrays (one per freq)
    data = []
    ntotal = 0
    print('Max data value:', sparse_data.max())
    for n in sparse_n:
        data.append(sparse_data[ntotal:ntotal+n])
        ntotal += n
    del sparse_n
    del sparse_data

    forward_i0 = sparse_i0
    forward_data = data

    # HACK -- reverse frequencies!!
    sparse_i0 = sparse_i0[::-1]
    data = list(reversed(data))
    print('Sparse_i0 range:', sparse_i0.min(), sparse_i0.max())
    #print('Sending fpga0:', fpga0)
    fpga_offset = int(fpga0 + (t0 / 2.56e-6))
    print('FPGA_offset:', fpga_offset)
    #injdata = InjectData(beam, 0, fpga_offset, sparse_i0, data)
    #R2 = client.inject_data(injdata, wait=True)
    #print('Results:', R2)

    # THIS produces detections in L1b!
    for i in range(30):
        print('Injecting again...')
        fpga_offset = int(fpga0 + ((t0 + (i*2)) / 2.56e-6))
        print('Injecting with FPGA_offset:', fpga_offset)
        injdata = InjectData(beam, 0, fpga_offset, sparse_i0, data)
        R2 = client.inject_data(injdata, wait=True)
        print('Results:', R2)

    # for i in range(30):
    #     print('Injecting again (forward)...')
    #     fpga_offset = int(fpga0 + ((t0 + (i*2+1)) / 2.56e-6))
    #     print('Injecting with FPGA_offset:', fpga_offset)
    #     injdata = InjectData(beam, 0, fpga_offset, forward_i0, forward_data)
    #     R2 = client.inject_data(injdata, wait=True)
    #     print('Results:', R2)
        
    #for i in range(15):
    #for i in range(30):
    for i in range(10):

        # # Write 2-ago?
        # min_fpga = (chunk0 + -2 + i) * 1024 * 384
        # max_fpga = (chunk0 + -2 + i+1) * 1024 * 384
        # fnpat = 'chunk-c(CHUNK).msgpack'
        # r = client.write_chunks([beam], min_fpga, max_fpga, fnpat, need_rfi=True,
        #                         waitAll=False)
        # print('r:', r)


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
    nchunks = ichunk - chunk0
    return nchunks
    

if __name__ == '__main__':
    main()
