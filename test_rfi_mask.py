from __future__ import print_function
import simulate_l0
import numpy as np
from rpc_client import read_msgpack_file, RpcClient
from time import sleep

if False:
    beam_id = 0
    fpga_counts_per_sample = 384
    nt = 1024
    nf = 16384
    nrfi = 1024
    nt_coarse = nt // 16
    nf_coarse = 1024
    
    data = np.clip(128. + 20. * np.random.normal(size=(nf, nt)), 0, 255)
    offset = np.empty((nf_coarse, nt_coarse), np.float64)
    scale = np.empty((nf_coarse, nt_coarse), np.float64)
    rfi = np.zeros((nrfi, nt), np.bool)
    
    offset[:,:] = -128.
    scale[:,:] = 1.
    
    for i in range(10):
        data = np.clip(128. + 20. * np.random.normal(size=(nf, nt)), 1, 254)
        data[:, i*50:i*50+100] = 200
        ichunk = i + 50
        
        ch = simulate_l0.assembled_chunk(beam_id, fpga_counts_per_sample, ichunk,
                                         data, offset, scale, rfi)
        ch.write('chunk-%02i.msgpack' % i)
    
    ch2 = read_msgpack_file('chunk.msgpack')


l0 = simulate_l0.l0sim('l0_configs/l0_rfi.yml', 1.0)

#l0.send_chunk_file(0, 'chunk-0000-000000000000.msgpack')

client = RpcClient({'a':'tcp://127.0.0.1:5555'})

for i in range(5):
    l0.send_chunk_file(0, 'chunk-%02i.msgpack' % i)

print()
sleep(3)

print('After sending 5 chunks:')
print(client.get_statistics())

print('Sending write request...')
res = client.write_chunks([0], 0, (50 + 4) * 384 * 1024,
                          'chunks-out-(FPGA0).msgpack', need_rfi=True,
                          waitAll=False)
print('Got write request result:', res)
reqs,token = res
req = reqs[0]
for c in req:
    print('  ', c)
sleep(1)

for c in req:
    print('Status of', c.filename, ':', client.get_writechunk_status(c.filename))

sleep(1)

print('Sending more data...')
for i in range(5, 10):
    l0.send_chunk_file(0, 'chunk-%02i.msgpack' % i)

    for c in req:
        s = client.get_writechunk_status(c.filename)
        print('Status of', c.filename, ':', s[0])
    
for i in range(10):
    sleep(1)
    print()
    for c in req:
        s = client.get_writechunk_status(c.filename)
        print('Status of', c.filename, ':', s[0])
    

#print('RPC client received tokens:', client.received)

