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
         a 3-element list whose first element is a version number,
        second a dictionary of header values, and
        third is the trigger array (vector of floats).
        '''
        self.version = msg[0]
        assert(self.version == 2)
        header = msg[1]
        for k in ['t0', 'fpgacounts0', 'max_dm', 'dt_sample',
                  'trigger_lag_dt', 'nt_chunk', 'ndm_coarse',
                  'ndm_fine', 'nt_coarse_per_chunk', 'nsm', 'nbeta',
                  'ntr_tot']:
            setattr(self, k, header[k])
        trigger_vec = msg[2]
        # if packed as binary...
        #self.trigger = np.fromstring(trigger_vec, dtype='<f4')
        self.trigger = np.array(trigger_vec)
        self.trigger = self.trigger.reshape((self.ndm_coarse, self.nsm, self.nbeta,
                                             self.nt_coarse_per_chunk))

    def __repr__(self):
        return 'BonsaiCoarseTrigger: FPGAcounts %i, trigger array shape %s' % (self.fpgacounts0, self.trigger.shape)


if __name__ == '__main__':
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.subscribe('')
    addr = 'tcp://*:6666'
    socket.bind(addr)

    i = 0
    while True:
        parts = socket.recv_multipart()
        print('Received:', len(parts), 'parts:', [len(p) for p in parts])
        p = parts[0]
        # 'p' is a string

        # For debugging/testing: write msgpack binaries to files.
        fn = 'msg-%04i.msgpack' % i
        i += 1
        f = open(fn, 'wb')
        f.write(p)
        f.close()
        print('Wrote', fn)

        msg = msgpack.unpackb(p)
        print('Received', len(msg), 'triggers:')
        for m in msg:
            t = BonsaiCoarseTrigger(m)
            print('  ', t)
            print('    t0', t.t0)
            print('    first value:', t.trigger.ravel()[0])


