from __future__ import print_function
import zmq
#import umsgpack as msgpack
import msgpack

import numpy as np

#import matplotlib
import pylab as plt

if __name__ == '__main__':
    context = zmq.Context()

    if True:
        n_l1_nodes = 4
        n_beams_per_l1_node = 4
        n_l0_nodes = 8
    else:
        n_l1_nodes = 1
        n_beams_per_l1_node = 1
        n_l0_nodes = 1

    rpc_port_l1_base = 5555
    udp_port_l0_base = 20000
    
    # Create sockets to talk to L1 nodes...
    sockets = []
    for i in range(n_l1_nodes):
        print('Connecting to L1 RPC server...', i)
        socket = context.socket(zmq.REQ)
        socket.connect('tcp://localhost:%i' % (rpc_port_l1_base + i))
        sockets.append(socket)

    token = 1
        
    print('Sending get_statistics requests...')
    for socket in sockets:
        msg = msgpack.packb(['get_statistics', token])
        socket.send(msg)

    beam_meta = []
    print('Waiting for get_statistics replies...')
    for i,socket in enumerate(sockets):
        hdr,msg = socket.recv_multipart()
        print('Received reply: %i bytes' % len(msg))
        reply = msgpack.unpackb(msg)
        beam_meta.append(reply)

    # make plots from replies
    npackets_grid = np.zeros((n_l0_nodes, n_l1_nodes), int)
    l0_addrs = {}
    for i in range(n_l0_nodes):
        for j in range(n_l1_nodes):
            l0_addrs['127.0.0.1:%i' % (udp_port_l0_base + i*n_l1_nodes + j)] = i
    nodestats = []
    for i,rep in enumerate(beam_meta):
        print('Node stats:', rep[0])
        print('Per-node packet counts:', rep[1])
        for r in rep[2:]:
            print('Beam:', r)
        nodestats.append(rep[0])
        for k,v in rep[1].items():
            j = l0_addrs[k]
            npackets_grid[j, i] = v

    plt.clf()
    plt.imshow(npackets_grid, interpolation='nearest', origin='lower')
    #vmin=0)
    plt.colorbar()
    plt.xlabel('L1 node number')
    plt.xticks(np.arange(n_l1_nodes))
    plt.ylabel('L0 node number')
    plt.title('Number of packets sent between L0/L1 nodes')
    plt.savefig('packets.png')

    plt.clf()
    plt.plot([n['count_stream_mismatch'] for n in nodestats], '.-',
             label='stream mismatch')
    plt.plot([n['count_assembler_queued'] for n in nodestats], '.-',
             label='assembler queued')
    plt.plot([n['count_assembler_hits'] for n in nodestats], '.-',
             label='assembler hits')
    plt.plot([n['count_assembler_misses'] for n in nodestats], '.-',
             label='assembler misses')
    plt.plot([n['count_assembler_drops'] for n in nodestats], '.-',
             label='assembler drops')
    plt.plot([n['count_packets_received'] for n in nodestats], '.-',
             label='packets: received')
    plt.plot([n['count_packets_good'] for n in nodestats], '.-',
             label='packets: good')
    plt.plot([n['count_packets_bad'] for n in nodestats], '.-',
             label='packets: bad')
    plt.plot([n['count_packets_dropped'] for n in nodestats], '.-',
             label='packets: dropped')
    plt.plot([n['count_beam_id_mismatch'] for n in nodestats], '.-',
             label='beam id mismatch')
    plt.xlabel('L1 node number')
    plt.xticks(np.arange(n_l1_nodes))
    plt.legend(fontsize=8)
    plt.yscale('symlog')
    plt.savefig('counts.png')

    
            
    print('Sending write_chunks requests...')
    for socket in sockets:
        beams = [0,1,2]
        minchunk = 0
        maxchunk = 5
        filename_pat = 'chunk-%02llu-chunk%08llu-py.msgpack'
        msg = (msgpack.packb(['write_chunks', token]) +
               msgpack.packb([beams, minchunk, maxchunk, filename_pat]))
        socket.send(msg)
    print('Waiting for write_chunks replies...')
    for socket in sockets:
        hdr,msg = socket.recv_multipart()
        print('Received reply: %i bytes' % len(msg))
        # print('Message:', repr(msg))
        rep = msgpack.unpackb(msg)
        print('Reply:', rep)

    # print('Sending get_chunks requests...')
    # for socket in sockets:
    #     beams = [0,1]
    #     minchunk = 0
    #     maxchunk = 4
    #     compress = False
    #     msg = (msgpack.packb('get_chunks') +
    #            msgpack.packb([beams, minchunk, maxchunk, compress]))
    #     socket.send(msg)
    # print('Waiting for get_chunks replies...')
    # for socket in sockets:
    #     #  Get the reply.
    #     msg = socket.recv()
    #     print('Received reply: %i bytes' % len(msg))
    #     rep = msgpack.unpackb(msg)
    #     print('Parsed reply:', type(rep))
    #     print('Number of chunks:', len(rep))
    #     for chunk in rep:
    #         remain = chunk
    #         hdr, version = remain[:2]
    #         remain = remain[2:]
    #         print('  header', hdr, 'version', version)
    #         compression, data_size = remain[:2]
    #         remain = remain[2:]
    #         print('  compression', compression, 'comp size', data_size)
    #         beam, nupfreq, nt_per_packet, fpga_counts, nt_coarse, nscales, ndata, ichunk, isample = remain[:9]
    #         remain = remain[9:]
    #         print('  beam', beam)
    #         print('  chunk', ichunk)
    #         scales, offsets, data = remain
    #         print('    scales:', type(scales), len(scales), ',',
    #               'offsets', type(offsets), len(offsets), ',',
    #               'data', type(data), len(data))
    #         ascales = np.fromstring(scales, dtype='<f4')
    #         print('    scales:', ascales[:16], '...', ascales.dtype, ascales.shape)
    #         aoffsets = np.fromstring(offsets, dtype='<f4')
    #         print('    offsets:', aoffsets[:16], '...', aoffsets.dtype, aoffsets.shape)
    #         adata = np.fromstring(data, dtype=np.uint8)
    #         print('    data:', adata[:16], '...', adata.dtype, adata.shape)

