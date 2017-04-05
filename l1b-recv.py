from __future__ import print_function
import zmq
import msgpack
import numpy as np

class BonsaiCoarseTrigger(object):
    def __init__(self, msgpacked):
        m = msgpacked
        #version = c[1]
        #assert(version == 1)
        # print('version', version)
        self.dm_coarse_graining_factor = m[0]
        self.ndm_coarse = m[1]
        self.ndm_fine = m[2]
        self.nt_coarse_per_chunk = m[3]
        self.nsm = m[4]
        self.nbeta = m[5]
        self.tm_stride_dm = m[6]
        self.tm_stride_sm = m[7]
        self.tm_stride_beta = m[8]
        self.ntr_tot = m[9]
        trigger_vec = m[10]
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
            print('  type', type(m), len(m))
            
            t = BonsaiCoarseTrigger(m)
            print('  trigger', t)
            print('  ', t.trigger.shape)
            print('  val', t.trigger.ravel()[0])


