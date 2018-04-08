from __future__ import print_function
import zmq
import msgpack
import time
import numpy as np

'''
Python client for the L1 RPC service.

The RPC service is fully asynchronous; each request sends an integer
"token", and all replies to that query include the token.  Thus it is
possible to send multiple requests and then later retrieve the
results.

This client can talk to multiple RPC servers at once.
'''

class AssembledChunk(object):
    def __init__(self, msgpacked_chunk):
        c = msgpacked_chunk
        # print('header', c[0])
        version = c[1]
        assert(version == 1)
        # print('version', version)
        compressed = c[2]
        # print('compressed?', compressed)
        compressed_size = c[3]
        # print('compressed size', compressed_size)
        self.beam = c[4]
        self.nupfreq = c[5]
        self.nt_per_packet = c[6]
        self.fpga_counts_per_sample = c[7]
        self.nt_coarse = c[8]
        self.nscales = c[9]
        self.ndata   = c[10]
        self.fpga0   = c[11]
        self.fpgaN   = c[12]
        self.binning = c[13]

        self.nt = self.nt_coarse * self.nt_per_packet

        scales  = c[14]
        offsets = c[15]
        data    = c[16]

        if compressed:
           import pybitshuffle
           data = pybitshuffle.decompress(data, self.ndata)

        # Convert to numpy arrays
        self.scales  = np.fromstring(scales , dtype='<f4')
        self.offsets = np.fromstring(offsets, dtype='<f4')
        self.scales  = self.scales .reshape((-1, self.nt_coarse))
        self.offsets = self.offsets.reshape((-1, self.nt_coarse))
        self.data = np.frombuffer(data, dtype=np.uint8)
        self.data = self.data.reshape((-1, self.nt))

    def decode(self):
        # Returns (intensities,weights) as floating-point
        nf = self.data.shape[1]

        #intensities = np.zeros((nf, self.nt), np.float32)
        #weights     = np.zeros((nf, self.nt), np.float32)
        #weights[(self.data > 0) * (self.data < 255)] = 1.

        # print('Data shape:', self.data.shape)
        # print('Scales shape:', self.scales.shape)
        # print('nupfreq:', self.nupfreq)
        # print('nt_per_packet:', self.nt_per_packet)

        intensities = (self.offsets.repeat(self.nupfreq, axis=0).repeat(self.nt_per_packet, axis=1) +
                       self.data * self.scales.repeat(self.nupfreq, axis=0).repeat(self.nt_per_packet, axis=1)).astype(np.float32)

        weights = ((self.data > 0) * (self.data < 255)) * np.float32(1.0)

        return intensities,weights
        

def read_msgpack_file(fn):
    f = open(fn, 'rb')
    m = msgpack.unpackb(f.read())
    return AssembledChunk(m)

# c = read_msgpack_file('chunk-beam0077-chunk00000094+01.msgpack')
# print('Got', c)


class WriteChunkReply(object):
    '''
    Python version of rpc.hpp : WriteChunks_Reply: the RPC server's
    reply to a WriteChunks request.
    '''
    def __init__(self, beam, fpga0, fpgaN, filename, success, err):
        self.beam = beam
        self.fpga0 = fpga0
        self.fpgaN = fpgaN
        self.filename = filename
        self.success = success
        self.error = err
        self.server = None

    def __str__(self):
        s = 'WriteChunkReply(beam %i, fpga %i + %i, filename %s' % (self.beam, self.fpga0, self.fpgaN, self.filename)
        if self.success:
            s += ', succeeded)'
        else:
            s += ', failed: "%s")' % self.error
        return s

    def __repr__(self):
        return str(self)

class PacketRate(object):
    def __init__(self, msgpack):
        self.start = msgpack[0]
        self.period = msgpack[1]
        self.packets = msgpack[2]

    def __str__(self):
        return 'PacketRate: start %g, period %g, packets: ' % (self.start, self.period) + str(self.packets)
        
class RpcClient(object):
    def __init__(self, servers, context=None, identity=None):
        '''
        *servers*: a dict of [key, address] entries, where each
          *address* is a string address of an RPC server: eg,
          'tcp://localhost:5555'.

        *context*: if non-None: the ZeroMQ context in which to create
           my socket.

        *identity*: (ignored; here for backward-compatibility)
        '''
        if context is None:
            self.context = zmq.Context()
        else:
            self.context = context

        self.servers = servers
        self.sockets = {}
        self.rsockets = {}
        for k,v in servers.items():
            self.sockets[k] = self.context.socket(zmq.DEALER)
            self.sockets[k].connect(v)
            self.rsockets[self.sockets[k]] = k

        self.token = 0
        # Buffer of received messages: token->[(message,socket), ...]
        self.received = {}

    def get_statistics(self, servers=None, wait=True, timeout=-1):
        '''
        Retrieves statistics from each server.  Return value is one
        item per server.  Each item is a list of dictionaries (string
        to int).
        '''
        if servers is None:
            servers = self.servers.keys()
        tokens = []
        for k in servers:
            self.token += 1
            req = msgpack.packb(['get_statistics', self.token])
            tokens.append(self.token)
            self.sockets[k].send(req)
        if not wait:
            return tokens
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        # We expect one message part for each token.
        return [msgpack.unpackb(p[0]) if p is not None else None
                for p in parts]

    def get_packet_rate(self, start=None, period=None, servers=None, wait=True, timeout=-1):
        if servers is None:
            servers = self.servers.keys()
        tokens = []

        if start is None:
            start = 0.
        if period is None:
            period = 0.

        for k in servers:
            self.token += 1
            req = msgpack.packb(['get_packet_rate', self.token])
            args = msgpack.packb([float(start), float(period)])
            tokens.append(self.token)
            self.sockets[k].send(req + args)
        if not wait:
            return tokens
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        #print('parts:', parts)
        # We expect one message part for each token.
        return [PacketRate(msgpack.unpackb(p[0])) if p is not None else None
                for p in parts]

    def get_packet_rate_history(self, l0nodes=None,
                                start=None, end=None, period=None,
                                servers=None, wait=True, timeout=-1):
        if servers is None:
            servers = self.servers.keys()
        tokens = []

        if start is None:
            start = 0.
        if end is None:
            end = 0.
        if period is None:
            period = 0.
        if l0nodes is None:
            l0nodes = ['sum']

        for k in servers:
            self.token += 1
            req = msgpack.packb(['get_packet_rate_history', self.token])
            args = msgpack.packb([float(start), float(end), float(period),
                                  l0nodes])
            tokens.append(self.token)
            self.sockets[k].send(req + args)
        if not wait:
            return tokens
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        # We expect one message part for each token.
        return [msgpack.unpackb(p[0]) if p is not None else None
                for p in parts]

    ## the acq_beams should perhaps be a list of lists of beam ids,
    ## one list per L1 server.
    def stream(self, acq_name, acq_dev='', acq_meta='', acq_beams=[],
               new_stream=True,
               servers=None, wait=True, timeout=-1):
        if servers is None:
            servers = self.servers.keys()
        tokens = []
        for k in servers:
            self.token += 1
            req = msgpack.packb(['stream', self.token])
            # Ensure correct argument types
            acq_name = str(acq_name)
            acq_dev = str(acq_dev)
            acq_meta = str(acq_meta)
            acq_beams = [int(b) for b in acq_beams]
            new_stream = bool(new_stream)
            args = msgpack.packb([acq_name, acq_dev, acq_meta, acq_beams,
                                  new_stream])
            tokens.append(self.token)
            self.sockets[k].send(req + args)
        if not wait:
            return tokens
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        # We expect one message part for each token.
        return [msgpack.unpackb(p[0]) if p is not None else None
                for p in parts]

    def stream_status(self, servers=None, wait=True, timeout=-1):
        if servers is None:
            servers = self.servers.keys()
        tokens = []
        for k in servers:
            self.token += 1
            req = msgpack.packb(['stream_status', self.token])
            tokens.append(self.token)
            self.sockets[k].send(req)
        if not wait:
            return tokens
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        # We expect one message part for each token.
        return [msgpack.unpackb(p[0]) if p is not None else None
                for p in parts]
    
    def list_chunks(self, servers=None, wait=True, timeout=-1):
        '''
        Retrieves lists of chunks held by each server.
        Return value is one list per server, containing a list of [beam, fpga0, fpga1, bitmask] entries.
        '''
        if servers is None:
            servers = self.servers.keys()
        tokens = []
        for k in servers:
            self.token += 1
            req = msgpack.packb(['list_chunks', self.token])
            tokens.append(self.token)
            self.sockets[k].send(req)
        if not wait:
            return tokens
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        # We expect one message part for each token.
        return [msgpack.unpackb(p[0]) if p is not None else None
                for p in parts]

    def write_chunks(self, beams, min_fpga, max_fpga, filename_pattern,
                     priority=0,
                     dm=0.,
                     dm_error=0.,
                     sweep_width=0.,
                     frequency_binning=0,
                     servers=None, wait=True, timeout=-1, waitAll=True):
        '''
        Asks the RPC servers to write a set of chunks to disk.

        *beams*: list of integer beams to writet to disk
        *min_fgpa*, *max_fpga*: range of FPGA-counts to write
        *filename_pattern*: printf filename pattern
        *priority*: of writes.

        When requesting a sweep (NOT CURRENTLY IMPLEMENTED!):
        *dm*, *dm_error*: floats, DM and uncertainty of the sweep to request
        *sweep_width*: float, range in seconds to retrieve around the sweep

        *frequency_binning*: int, the factor by which to bin frequency
         data before writing.
        
        *wait*: wait for the initial replies listing the chunks to be written out.
        *waitAll*: wait for servers to reply that all chunks have been written out.
        '''
        if servers is None:
            servers = self.servers.keys()
        # Send RPC requests
        tokens = []
        for k in servers:
            self.token += 1
            hdr = msgpack.packb(['write_chunks', self.token])
            req = msgpack.packb([beams, min_fpga, max_fpga, dm, dm_error, sweep_width, frequency_binning, filename_pattern, priority])
            tokens.append(self.token)
            self.sockets[k].send(hdr + req)
        if not wait:
            return tokens
        # This will wait for the initial replies from servers, listing
        # the chunks to be written out.
        parts,servers = self.wait_for_tokens(tokens, timeout=timeout, get_sockets=True)
        # We expect one message part for each token.
        chunklists = [msgpack.unpackb(p[0]) if p is not None else None
                      for p in parts]
        #print('Chunklists:', chunklists)
        #print('Servers:', servers)
        if not waitAll:
            # Parse the results into WriteChunkReply objects, and add
            # the .server value.
            results = []
            for chunks,server in zip(chunklists, servers):
                if chunks is None:
                    results.append(None)
                    continue
                rr = []
                for chunk in chunks:
                    res = WriteChunkReply(*chunk)
                    res.server = self.rsockets[server]
                    rr.append(res)
                results.append(rr)
            return results, tokens

        return self.wait_for_all_writes(chunklists, tokens,
                                        timeout=timeout)

    def wait_for_all_writes(self, chunklists, tokens, timeout=-1):
        ## Wait for notification that all writes from a write_chunks
        ## call have completed.
        results = []
        if timeout > 0:
            t0 = time.time()
        for token,chunklist in zip(tokens, chunklists):
            if chunklist is None:
                # Did not receive a reply from this server.
                continue
            N = len(chunklist)
            n = 0
            while n < N:
                [p],[s] = self.wait_for_tokens([token], timeout=timeout,
                                               get_sockets=True)
                # adjust timeout
                if timeout > 0:
                    tnow = time.time()
                    timeout = max(0, t0 + timeout - tnow)
                    t0 = tnow
                if p is None:
                    # timed out
                    break
                chunk = msgpack.unpackb(p[0])
                #print('got chunk', chunk)
                n += 1
                # unpack the reply
                [beam, fpga0, fpgaN, filename, success, err] = chunk
                res = WriteChunkReply(beam, fpga0, fpgaN, filename, success, err)
                res.server = self.rsockets[s]
                results.append(res)
        return results

    def get_writechunk_status(self, filename,
                              servers=None, wait=True, timeout=-1):
        '''
        Asks the RPC servers for the status of a given filename whose
        write was requested by a write_chunks request.

        Returns (status, error_message).
        '''
        if servers is None:
            servers = self.servers.keys()
        # Send RPC requests
        tokens = []
        for k in servers:
            self.token += 1
            hdr = msgpack.packb(['get_writechunk_status', self.token])
            req = msgpack.packb(filename)
            tokens.append(self.token)
            self.sockets[k].send(hdr + req)
        if not wait:
            return tokens
        # Wait for results...
        results = self.wait_for_tokens(tokens, timeout=timeout)
        # We only expect one response per server
        results = [msgpack.unpackb(p[0]) if p is not None else None
                   for p in results]
        return results

    def shutdown(self, servers=None):
        '''
        Sends a shutdown request to each RPC server.
        '''
        if servers is None:
            servers = self.servers.keys()
        # Send RPC requests
        tokens = []
        for k in servers:
            self.token += 1
            hdr = msgpack.packb(['shutdown', self.token])
            tokens.append(self.token)
            self.sockets[k].send(hdr)

    def start_logging(self, address, servers=None):
        '''
        Sends a request to start chlog logging to the given address.
        '''
        if servers is None:
            servers = self.servers.keys()
        # Send RPC requests
        tokens = []
        for k in servers:
            self.token += 1
            hdr = msgpack.packb(['start_logging', self.token])
            req = msgpack.packb(address)
            tokens.append(self.token)
            self.sockets[k].send(hdr + req)

    def stop_logging(self, address, servers=None):
        '''
        Sends a request to stop chlog logging to the given address.
        '''
        if servers is None:
            servers = self.servers.keys()
        # Send RPC requests
        tokens = []
        for k in servers:
            self.token += 1
            hdr = msgpack.packb(['stop_logging', self.token])
            req = msgpack.packb(address)
            tokens.append(self.token)
            self.sockets[k].send(hdr + req)
            
    def _pop_token(self, t, d=None, get_socket=False):
        '''
        Pops a message for the given token number *t*, or returns *d*
        if one does not exist.
        '''
        try:
            msgs = self.received[t]
        except KeyError:
            return d
        msg = msgs.pop(0)
        # if list of messages for this token is now empty, delete it
        if len(msgs) == 0:
            del self.received[t]
        # self.received actually contains a list of (socket,message) tuples,
        # so drop the socket part if not requested by the caller
        if get_socket:
            return msg
        msg = msg[1]
        return msg

    def _receive(self, timeout=-1):
        '''
        Receives >=1 replies from servers.

        *timeout* in milliseconds.  timeout=0 means only read
         already-queued messages.  timeout=-1 means wait forever.

        Returns True if >1 messages were received.
        '''
        def _handle_parts(parts, socket):
            hdr  = parts[0]
            token = msgpack.unpackb(hdr)
            rest = parts[1:]
            #print('Received token:', token, ':', rest)
            if token in self.received:
                self.received[token].append((socket, rest))
            else:
                self.received[token] = [(socket, rest)]
            #print('Now received parts for token:', self.received[token])

        received = False

        poll = zmq.Poller()
        for k,v in self.sockets.items():
            poll.register(v, zmq.POLLIN)

        # Read all the messages that are waiting (non-blocking read)
        while True:
            events = poll.poll(timeout=0)
            if len(events) == 0:
                break
            for s,e in events:
                #print('Received reply on socket', s)
                parts = s.recv_multipart()
                _handle_parts(parts, s)
                received = True
        if received:
            return True
                
        if timeout == 0:
            # Don't do a blocking read
            return received

        # Do one blocking read.
        events = poll.poll(timeout)
        for s,e in events:
            #print('Receive()')
            parts = s.recv_multipart()
            _handle_parts(parts, s)
            received = True

        return received
    
    def wait_for_tokens(self, tokens, timeout=-1, get_sockets=False):
        '''
        Retrieves results for the given *tokens*, possibly waiting for
        servers to reply.

        Returns a list of result messages, one for each *token*.
        '''

        #print('Waiting for tokens (timeout', timeout, '):', tokens)

        results = {}
        if get_sockets:
            sockets = {}
        todo = [token for token in tokens]
        if timeout > 0:
            t0 = time.time()
        while len(todo):
            #print('Still waiting for tokens:', todo)
            done = []
            for token in todo:
                r = self._pop_token(token, get_socket=get_sockets)
                if r is not None:
                    #print('Popped token', token, '->', r)
                    done.append(token)
                    if get_sockets:
                        # unpack socket,result tuple
                        socket,r = r
                        sockets[token] = socket
                    results[token] = r
            for token in done:
                todo.remove(token)
            if len(todo):
                #print('Receiving (timeout', timeout, ')')
                if not self._receive(timeout=timeout):
                    # timed out
                    #print('timed out')
                    break
                # adjust timeout
                if timeout > 0:
                    tnow = time.time()
                    timeout = max(0, t0 + timeout - tnow)
                    t0 = tnow
        if not get_sockets:
            return [results.get(token, None) for token in tokens]
        return ([results.get(token, None) for token in tokens],
                [sockets.get(token, None) for token in tokens])

import threading

class ChLogServer(threading.Thread):
    def __init__(self, addr='127.0.0.1', port=None, context=None):
        super(ChLogServer, self).__init__()
        # I'm a demon thread! Muahahaha
        self.daemon = True

        if context is None:
            self.context = zmq.Context()
        else:
            self.context = context

        self.socket = self.context.socket(zmq.SUB)
        #self.socket.set(zmq.SUBSCRIBE, '')
        self.socket.subscribe('')
        
        addr = 'tcp://' + addr
        if port is None:
            addr += ':*'
            self.socket.bind(addr)
            addr = self.socket.get(zmq.LAST_ENDPOINT)
        else:
            addr += ':' + str(port)
            self.socket.bind(addr)
        print('Bound to', addr)
        self.address = addr
        
        self.start()

    def run(self):
        while True:
            parts = self.socket.recv_multipart()
            print('Received:', parts)
        
        
if __name__ == '__main__':
    import argparse
    import sys

    parser = argparse.ArgumentParser()
    parser.add_argument('--shutdown', action='store_true',
                        help='Send shutdown RPC message?')
    parser.add_argument('--log', action='store_true',
                        help='Start up chlog server?')
    parser.add_argument('--write', '-w', nargs=4, metavar='x',#['<comma-separated beams>', '<minfpga>', '<maxfpga>', '<filename-pattern>'],
                        help='Send write_chunks command: <comma-separated beams> <minfpga> <maxfpga> <filename-pattern>', action='append',
                        default=[])
    parser.add_argument('--awrite', nargs=4, metavar='x',
                        help='Send async write_chunks command: <comma-separated beams> <minfpga> <maxfpga> <filename-pattern>', action='append',
        default=[])
    parser.add_argument('--list', action='store_true', default=False,
                        help='Just send list_chunks command and exit.')
    parser.add_argument('--identity', help='(ignored)')
    parser.add_argument('--stream', help='Stream to files')
    parser.add_argument('--stream-base', help='Stream base directory')
    parser.add_argument('--stream-meta', help='Stream metadata', default='')
    parser.add_argument('--stream-beams', action='append', default=[], help='Stream a subset of beams.  Can be a comma-separated list of integers.  Can be repeated.')
    
    parser.add_argument('--rate', action='store_true', default=False,
                        help='Send packet rate matrix request')
    parser.add_argument('--rate-history', action='store_true', default=False,
                        help='Send packet rate history request')
    parser.add_argument('--l0', action='append', default=[],
                        help='Request rate history for the list of L0 nodes')
    parser.add_argument('ports', nargs='*',
                        help='Addresses or port numbers of RPC servers to contact')
    opt = parser.parse_args()
    args = opt.ports

    if len(args):
        servers = {}
        for i,a in enumerate(args):
            port = None
            try:
                port = int(a)
                port = 'tcp://127.0.0.1:%i' % port
            except:
                port = a

            servers[chr(ord('a')+i)] = port
    else:
        servers = dict(a='tcp://127.0.0.1:5555',
                       b='tcp://127.0.0.1:5556')

    print('Sending to servers:', servers)
    client = RpcClient(servers)

    if opt.log:
        logger = ChLogServer()
        addr = logger.address
        client.start_logging(addr)

    doexit = False

    if opt.list:
        chunks = client.list_chunks()
        print('Received chunk list:', len(chunks))
        for chunklist in chunks:
            for beam,f0,f1,where in chunklist:
                print('  beam %4i, FPGA range %i to %i' % (beam, f0, f1))
        doexit = True

    if opt.stream:
        beams = []
        for b in opt.stream_beams:
            # Parse possible comma-separate list of strings
            words = b.split(',')
            for w in words:
                beams.append(int(w))
        patterns = client.stream(opt.stream, opt.stream_base, opt.stream_meta, beams)
        print('Streaming to:', patterns)
        doexit = True

    if opt.rate:
        rates = client.get_packet_rate()
        print('Received packet rates:')
        for r in rates:
            print('Got rate:', r)
        doexit = True

    if opt.rate_history:
        kwa = {}
        if len(opt.l0):
            kwa.update(l0nodes=opt.l0)
        rates = client.get_packet_rate_history(start=-20, **kwa)
        print('Received packet rate history:')
        print(rates)
        doexit = True
        
    if len(opt.write):
        for beams, f0, f1, fnpat in opt.write:
            beams = beams.split(',')
            beams = [int(b,10) for b in beams]
            f0 = int(f0, 10)
            f1 = int(f1, 10)
            R = client.write_chunks(beams, f0, f1, fnpat)
            print('Results:')
            if R is not None:
                for r in R:
                    print('  ', r)
        doexit = True

    # Async write requests
    if len(opt.awrite):
        for beams, f0, f1, fnpat in opt.awrite:
            beams = beams.split(',')
            beams = [int(b,10) for b in beams]
            f0 = int(f0, 10)
            f1 = int(f1, 10)
            R = client.write_chunks(beams, f0, f1, fnpat, waitAll=False)
            print('Results:')
            if R is not None:
                for r in R:
                    print('  ', r)
        doexit = True

    if doexit:
        sys.exit(0)
    
    print('get_statistics()...')
    stats = client.get_statistics(timeout=3000)
    print('Got stats:', stats)

    print('list_chunks()...')
    stats = client.list_chunks(timeout=3000)
    print('Got chunks:', stats)
    
    print()
    print('write_chunks()...')

    minfpga = 38600000
    #maxfpga = 38600000
    maxfpga = 48600000

    R = client.write_chunks([77,78], minfpga, maxfpga, 'chunk-beam(BEAM)-chunk(CHUNK)+(NCHUNK).msgpack', timeout=3000)
    print('Got:', R)

    for r in R:
        servers = [r.server]
        print('Received', r, 'from server:', r.server)
        X = client.get_writechunk_status(r.filename, servers=servers)
        X = X[0]
        print('Result:', X)

    print('Bogus result:', client.get_writechunk_status('nonesuch'))

    chunks,tokens = client.write_chunks([77,78], minfpga, maxfpga, 'chunk2-beam(BEAM)-chunk(CHUNK)+(NCHUNK).msgpack', waitAll=False)
    print('Got chunks:', chunks)
    for chlist in chunks:
        if chlist is None:
            continue
        for chunk in chlist:
            print('Chunk:', chunk)
            [R] = client.get_writechunk_status(chunk.filename, servers=[chunk.server])
            print('Status:', R)
    time.sleep(1)
    for chlist in chunks:
        if chlist is None:
            continue
        for chunk in chlist:
            print('Chunk:', chunk)
            [R] = client.get_writechunk_status(chunk.filename, servers=[chunk.server])
            print('Status:', R)

    if opt.log:
        addr = logger.address
        client.stop_logging(addr)
    
    if opt.shutdown:
        client.shutdown()
        time.sleep(2)
