from __future__ import print_function
import zmq
import msgpack
import time

'''
Python client for the L1 RPC service.

The RPC service is fully asynchronous; each request sends an integer
"token", and all replies to that query include the token.  Thus it is
possible to send multiple requests and then later retrieve the
results.

This client can talk to multiple RPC servers at once.
'''

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

    def __str__(self):
        s = 'WriteChunkReply(beam %i, fpga %i + %i, filename %s' % (self.beam, self.fpga0, self.fpgaN, self.filename)
        if self.success:
            s += ', succeeded)'
        else:
            s += ', failed: "%s")' % self.error
        return s

    def __repr__(self):
        return str(self)

class RpcClient(object):
    def __init__(self, servers, context=None, identity=None):
        '''
        *servers*: a dict of [key, address] entries, where each
          *address* is a string address of an RPC server: eg,
          'tcp://localhost:5555'.

        *context*: if non-None: the ZeroMQ context in which to create
           my socket.

        *identity*: if non-None: the name to give our ZeroMQ socket
           (string).
        '''
        if context is None:
            self.context = zmq.Context()
        else:
            self.context = context

        self.servers = servers
        self.sockets = {}
        for k,v in servers.items():
            self.sockets[k] = self.context.socket(zmq.DEALER)
            if identity is not None:
                self.sockets[k].set(zmq.IDENTITY, identity)
            self.sockets[k].connect(v)

        self.token = 0
        # Buffer of received messages: token->message
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
                     servers=None, wait=True, timeout=-1, waitAll=True):
        '''
        Asks the RPC servers to write a set of chunks to disk.

        *beams*: list of integer beams to writet to disk
        *min_fgpa*, *max_fpga*: range of FPGA-counts to write
        *filename_pattern*: printf filename pattern
        *priority*: of writes.
        
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
            req = msgpack.packb([beams, min_fpga, max_fpga, filename_pattern, priority])
            tokens.append(self.token)
            self.sockets[k].send(hdr + req)
        if not wait:
            return tokens
        # This will wait for the initial replies from servers, listing
        # the chunks to be written out.
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        # We expect one message part for each token.
        chunklists = [msgpack.unpackb(p[0]) if p is not None else None
                      for p in parts]
        #print('Lists of chunks expected:', chunklists)
        if not waitAll:
            return chunklists

        ## Wait for notification that all writes have completed.
        results = []
        if timeout > 0:
            t0 = time.time()
        for k,token,chunklist in zip(servers, tokens, chunklists):
            #print('Waiting for', N, 'chunks from', k, ', token', token)
            if chunklist is None:
                # Did not receive a reply from this server.
                continue
            N = len(chunklist)
            n = 0
            while n < N:
                #print('Waiting for', n, 'chunks from', k, ', token', token, 'got', len(rr))
                [p] = self.wait_for_tokens([token], timeout=timeout)
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
                results.append(res)

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
        
    def _pop_token(self, t, d=None):
        '''
        Pops a message for the given token number *t*, or returns *d*
        if one does not exist.
        '''
        try:
            msgs = self.received[t]
        except KeyError:
            return d
        msg = msgs.pop(0)
        if len(msgs) == 0:
            del self.received[t]
        return msg

    def _receive(self, timeout=-1):
        '''
        Receives >=1 replies from servers.

        *timeout* in milliseconds.  timeout=0 means only read
         already-queued messages.  timeout=-1 means wait forever.

        Returns True if >1 messages were received.
        '''
        def _handle_parts(parts):
            hdr  = parts[0]
            token = msgpack.unpackb(hdr)
            rest = parts[1:]
            #print('Received token:', token, ':', rest)
            if token in self.received:
                self.received[token].append(rest)
            else:
                self.received[token] = [rest]
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
                #print('Receive()')
                parts = s.recv_multipart()
                _handle_parts(parts)
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
            _handle_parts(parts)
            received = True

        return received
    
    def wait_for_tokens(self, tokens, timeout=-1):
        '''
        Retrieves results for the given *tokens*, possibly waiting for
        servers to reply.

        Returns a list of result messages, one for each *token*.
        '''
        results = {}
        todo = [token for token in tokens]
        if timeout > 0:
            t0 = time.time()
        while len(todo):
            #print('Still waiting for tokens:', todo)
            done = []
            for token in todo:
                r = self._pop_token(token)
                if r is not None:
                    done.append(token)
                    results[token] = r
            for token in done:
                todo.remove(token)
            if len(todo):
                if not self._receive(timeout=timeout):
                    # timed out
                    break
                # adjust timeout
                if timeout > 0:
                    tnow = time.time()
                    timeout = max(0, t0 + timeout - tnow)
                    t0 = tnow
        return [results.get(token, None) for token in tokens]

if __name__ == '__main__':
    import argparse
    import sys

    parser = argparse.ArgumentParser()
    parser.add_argument('--shutdown', action='store_true',
                        help='Send shutdown RPC message?')
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

    client = RpcClient(servers, identity='client')

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

    if opt.shutdown:
        client.shutdown()
        time.sleep(2)
