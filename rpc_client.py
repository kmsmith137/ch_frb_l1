from __future__ import print_function
import zmq
#import umsgpack as msgpack
import msgpack

import numpy as np

if __name__ == '__main__':
    context = zmq.Context()

    #  Socket to talk to server
    print('Connecting to L1 RPC server...')
    socket = context.socket(zmq.REQ)
    socket.connect('tcp://localhost:5555')

    print('Sending get_beam_metadata request...')
    msg = msgpack.packb('get_beam_metadata')
    socket.send(msg)
    #  Get the reply.
    msg = socket.recv()
    print('Received reply: %i bytes' % len(msg))
    print('Message:', repr(msg))
    rep = msgpack.unpackb(msg)
    print('Reply:', rep)


    print('Sending get_chunks request...')
    msg = (msgpack.packb('get_chunks') +
           msgpack.packb([1, 3]) +
           msgpack.packb(1) +
           msgpack.packb(100))
    socket.send(msg)
    #  Get the reply.
    msg = socket.recv()
    print('Received reply: %i bytes' % len(msg))
    rep = msgpack.unpackb(msg)
    print('Parsed reply:', type(rep))
    print('Number of beams:', len(rep))
    for beamchunk in rep:
        print('  beamchunk:', len(beamchunk))
        for chunk in beamchunk:
            print('    chunk:', type(chunk))
            print('    chunk:', chunk[:9])
            nscales = chunk[5]
            ndata = chunk[6]
            scales = chunk[9]
            offsets = chunk[10]
            data = chunk[11]
            print('    scales:', type(scales), len(scales),
                  'offsets', type(offsets), len(offsets),
                  'data', type(data), len(data))
            ascales = np.fromstring(scales, dtype='<f4')
            print('Scales:', ascales[:16])
            aoffsets = np.fromstring(offsets, dtype='<f4')
            print('Offsets:', aoffsets[:16])
            adata = np.fromstring(data, dtype=np.uint8)
            print('Data:', adata[:16])

    print('Sending get_chunks_2 request...')
    ## guess about the format...
    msg = (msgpack.packb('get_chunks_2') +
           msgpack.packb([[2], 1, 100]))
    socket.send(msg)
    #  Get the reply.
    msg = socket.recv()
    print('Received reply: %i bytes' % len(msg))
    rep = msgpack.unpackb(msg)
    print('Parsed reply:', type(rep))
    print('Number of beams:', len(rep))


