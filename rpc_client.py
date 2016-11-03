from __future__ import print_function
import zmq
#import umsgpack as msgpack
import msgpack

import numpy as np

if __name__ == '__main__':
    context = zmq.Context()

    n_l1_nodes = 1
    n_beams_per_l1_node = 1
    rpc_port_l1_base = 5555;
    
    # Create sockets to talk to L1 nodes...
    sockets = []
    for i in range(n_l1_nodes):
        print('Connecting to L1 RPC server...', i)
        socket = context.socket(zmq.REQ)
        socket.connect('tcp://localhost:%i' % (rpc_port_l1_base + i))
        sockets.append(socket)

    print('Sending get_beam_metadata requests...')
    for socket in sockets:
        msg = msgpack.packb('get_beam_metadata')
        socket.send(msg)
    print('Waiting for get_beam_metadata replies...')
    for socket in sockets:
        #  Get the reply.
        msg = socket.recv()
        print('Received reply: %i bytes' % len(msg))
        # print('Message:', repr(msg))
        rep = msgpack.unpackb(msg)
        print('Reply:', rep)


    print('Sending write_chunks requests...')
    for socket in sockets:
        beams = [0,1,2]
        minchunk = 0
        maxchunk = 5
        filename_pat = 'chunk-%02llu-chunk%08llu-py.msgpack'
        msg = (msgpack.packb('write_chunks') +
               msgpack.packb([beams, minchunk, maxchunk, filename_pat]))
        socket.send(msg)
    print('Waiting for write_chunks replies...')
    for socket in sockets:
        #  Get the reply.
        msg = socket.recv()
        print('Received reply: %i bytes' % len(msg))
        # print('Message:', repr(msg))
        rep = msgpack.unpackb(msg)
        print('Reply:', rep)

    print('Sending get_chunks requests...')
    for socket in sockets:
        beams = [0,1]
        minchunk = 0
        maxchunk = 4
        compress = False
        msg = (msgpack.packb('get_chunks') +
               msgpack.packb([beams, minchunk, maxchunk, compress]))
        socket.send(msg)
    print('Waiting for get_chunks replies...')
    for socket in sockets:
        #  Get the reply.
        msg = socket.recv()
        print('Received reply: %i bytes' % len(msg))
        rep = msgpack.unpackb(msg)
        print('Parsed reply:', type(rep))
        print('Number of chunks:', len(rep))
        for chunk in rep:
            remain = chunk
            hdr, version = remain[:2]
            remain = remain[2:]
            print('  header', hdr, 'version', version)
            compression, data_size = remain[:2]
            remain = remain[2:]
            print('  compression', compression, 'comp size', data_size)
            beam, nupfreq, nt_per_packet, fpga_counts, nt_coarse, nscales, ndata, ichunk, isample = remain[:9]
            remain = remain[9:]
            print('  beam', beam)
            print('  chunk', ichunk)
            scales, offsets, data = remain
            print('    scales:', type(scales), len(scales), ',',
                  'offsets', type(offsets), len(offsets), ',',
                  'data', type(data), len(data))
            ascales = np.fromstring(scales, dtype='<f4')
            print('    scales:', ascales[:16], '...', ascales.dtype, ascales.shape)
            aoffsets = np.fromstring(offsets, dtype='<f4')
            print('    offsets:', aoffsets[:16], '...', aoffsets.dtype, aoffsets.shape)
            adata = np.fromstring(data, dtype=np.uint8)
            print('    data:', adata[:16], '...', adata.dtype, adata.shape)

