from __future__ import print_function
import zmq
import msgpack
import numpy as np

chime_seconds_per_fpga_count = 2.56e-6

class BonsaiCoarseTrigger(object):
    def __init__(self, msgpacked):
        m = msgpacked
        self.version = m[0]
        assert(self.version == 1)
        m = m[1:]
        (self.t0, self.max_dm, self.dt_sample, self.trigger_lag_dt,
         self.nt_chunk, self.dm_coarse_graining_factor, self.ndm_coarse,
         self.ndm_fine, self.nt_coarse_per_chunk, self.nsm,
         self.nbeta, self.tm_stride_dm, self.tm_stride_sm, self.tm_stride_beta,
         self.ntr_tot) = m[:15]
        self.fpga_0 = self.t0 / chime_seconds_per_fpga_count
        m = m[15:]
        trigger_vec = m[0]
        # if packed as binary...
        #self.trigger = np.fromstring(trigger_vec, dtype='<f4')
        self.trigger = np.array(trigger_vec)
        self.trigger = self.trigger.reshape((self.ndm_coarse, self.nsm, self.nbeta,
                                             self.nt_coarse_per_chunk))

if __name__ == '__main__':
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.subscribe('')
    addr = 'tcp://127.0.0.1:6666'
    socket.bind(addr)

    while True:
        parts = socket.recv_multipart()
        print('Received:', len(parts), 'parts:', [len(p) for p in parts])
        p = parts[0]
        msg = msgpack.unpackb(p)
        print('Message:', len(msg))
        for m in msg:
            #print('  type', type(m), len(m))
            t = BonsaiCoarseTrigger(m)
            print('  trigger', t)
            print('  t0', t.t0)
            print('  fpgacounts:', t.fpga_0)
            print('  ', t.trigger.shape)
            print('  val', t.trigger.ravel()[0])


