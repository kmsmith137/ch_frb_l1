import simulate_l0
import numpy as np
from rpc_client import read_msgpack_file

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

ch = simulate_l0.assembled_chunk(beam_id, fpga_counts_per_sample,
                                 data, offset, scale, rfi)

ch.write('chunk.msgpack')

ch2 = read_msgpack_file('chunk.msgpack')


l0 = simulate_l0.l0sim('l0_configs/l0_rfi.yml', 1.0)
print(dir(l0))

l0.send_chunk_file(0, 'chunk-0000-000000000000.msgpack')

