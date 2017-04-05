from __future__ import print_function
import zmq
import msgpack
import numpy as np

class BonsaiCoarseTrigger(object):
    '''
    Code to unpack a msgpacked Bonsai coarse trigger event, as sent
    from L1 to L1b.  These are basically just a list of a few scalars
    followed by a float array of trigger values (4-d array, but sent
    as a flat list).

    The parameters in here have the same names as the bonsai.hpp :
    coarse_trigger_set struct, or bonsai::config_params, with the addition of:

    * fpgacounts0: (uint64_t) fpgacounts value at the start of the chunk
    * t0: (double) time value (computed from fpgacounts0) at start of chunk
    '''
    def __init__(self, msg):
        '''
        *msg*: msgpack-unpacked data to be interpreted.  This will be
         a list; this code unpacks elements from that list.
        '''
        self.version = msg[0]
        assert(self.version == 1)
        msg = msg[1:]
        (self.t0, self.fpgacounts0,
         self.max_dm, self.dt_sample, self.trigger_lag_dt,
         self.nt_chunk, self.dm_coarse_graining_factor, self.ndm_coarse,
         self.ndm_fine, self.nt_coarse_per_chunk, self.nsm,
         self.nbeta, self.tm_stride_dm, self.tm_stride_sm, self.tm_stride_beta,
         self.ntr_tot) = msg[:16]
        msg = msg[16:]
        trigger_vec = msg[0]
        # if packed as binary...
        #self.trigger = np.fromstring(trigger_vec, dtype='<f4')
        self.trigger = np.array(trigger_vec)
        self.trigger = self.trigger.reshape((self.ndm_coarse, self.nsm, self.nbeta,
                                             self.nt_coarse_per_chunk))

if __name__ == '__main__':
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    addr = 'tcp://127.0.0.1:6666'
    socket.bind(addr)
    socket.subscribe('')

    i = 0
    while True:
        parts = socket.recv_multipart()
        print('Received:', len(parts), 'parts:', [len(p) for p in parts])
        p = parts[0]
        # 'p' is a string
        fn = 'msg-%04i.msgpack' % i
        i += 1
        f = open(fn, 'wb')
        f.write(p)
        f.close()
        print('Wrote', fn)

        msg = msgpack.unpackb(p)
        print('Message:', len(msg))
        for m in msg:
            #print('  type', type(m), len(m))
            t = BonsaiCoarseTrigger(m)
            print('  trigger', t)
            print('  t0', t.t0)
            print('  fpgacounts:', t.fpgacounts0)
            print('  ', t.trigger.shape)
            print('  val', t.trigger.ravel()[0])


