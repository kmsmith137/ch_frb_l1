from __future__ import print_function
import zmq
import msgpack
import numpy as np
import os

if __name__ == '__main__':
    import sys
    args = sys.argv[1:]
    dest = 'tcp://127.0.0.1:6666'
    if len(args):
        dest = args[0]
        print('Sending to L1b:', dest)
        
    context = zmq.Context()
    socket = context.socket(zmq.PUB)

    i = 0
    while True:
        fn = 'msg-%04i.msgpack' % i
        i += 1
        print('Reading', fn)
        if not os.path.exists(fn):
            print('Does not exist:', fn)
            break
        p = open(fn).read()
        print('Sending', len(p))
        socket.send(p)
