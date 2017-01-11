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
        self.socket = self.context.socket(zmq.ROUTER)
        self.socket.setsockopt(zmq.ROUTER_MANDATORY, 1)
        if identity is not None:
            self.socket.set(zmq.IDENTITY, identity)
        self.servers = servers
        for k,v in servers.items():
            pass
            #print('socket.connect(', v, ')')
            #self.socket.connect(v)
            # Send initial message to server to establish routing table
            #self.socket.send_multipart([v])
        self.token = 0
        # Buffer of received messages: token->message
        self.received = {}
        
    def wait_for_setup(self, wait=0.1, ntimes=20):
        #todo = servers.keys()
        #while len(todo):
        #    k = todo[0]
        for k in servers.keys():
            print('Connect to', servers[k])
            self.socket.connect(servers[k])
            time.sleep(1)
            for i in range(ntimes):
                try:
                    print('Trying to send:', k, '->', self.servers[k])
                    self.token += 1
                    msgs = [self.servers[k], msgpack.packb(['hello', self.token])]
                    print('sending:', msgs)
                    self.socket.send_multipart(msgs)
                    #self.socket.send_multipart([self.servers[k], 'hello'])
                    print('sent')
                    #todo.remove(k)
                    break
                except zmq.error.ZMQError as e:
                    print('send error; retrying')
                    print(e)
                    time.sleep(wait)
            else:
                break
        raise RuntimeError('Failed to set up connection with servers: ' + str(todo))
            
    def get_statistics(self, servers=None, wait=True, timeout=-1):
        if servers is None:
            servers = self.servers.keys()
        tokens = []
        for k in servers:
            self.token += 1
            req = msgpack.packb(['get_statistics', self.token])
            tokens.append(self.token)
            print('sending to', self.servers[k])
            self.socket.send_multipart([self.servers[k], req])
        if not wait:
            return tokens
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        print('Received parts:', parts)
        # We expect one message, with one message part, for each token.
        return [msgpack.unpackb(p[0][0]) for p in parts]

    def write_chunks(self, beams, min_fpga, max_fpga, filename_pattern,
                     priority=0,
                     servers=None, wait=True, timeout=-1, waitAll=True):
        '''
        *wait*: wait for the initial replies listing the chunks to be written out.
        *waitAll*: wait for servers to reply that all chunks have been written out.
        '''
        if servers is None:
            servers = self.servers.keys()
        tokens = []
        for k in servers:
            self.token += 1
            hdr = msgpack.packb(['write_chunks', self.token])
            req = msgpack.packb([beams, min_fpga, max_fpga, filename_pattern,
                                 priority])
            tokens.append(self.token)
            self.socket.send_multipart([self.servers[k], hdr + req])
        if not wait:
            return tokens
        # This will wait for the initial replies from servers, listing
        # the chunks to be written out.
        parts = self.wait_for_tokens(tokens, timeout=timeout)
        print('Received parts:', parts)
        # We expect one message, with one message part, for each token.
        chunklists = [msgpack.unpackb(p[0][0]) for p in parts]
        print('Lists of chunks:', chunklists)
        if not waitAll:
            return chunklists

        ## Wait for more...

        ## NOTE -- *parts* may already contain some of the chunk
        ## notifications.


        
        
    def _pop_token(self, k, d=None):
        return self.received.pop(k, d)

    def _receive(self, timeout=-1):
        '''
        Receives >=1 replies from servers.

        *timeout* in milliseconds.  timeout=0 means only read
         already-queued messages.  timeout=-1 means wait forever.

        Returns True if >1 messages were received.
        '''
        def _handle_parts(parts):
            server = parts[0]
            hdr  = parts[1]
            token = msgpack.unpackb(hdr)
            print('Received token:', token)
            rest = parts[2:]
            if token in self.received:
                self.received.append(rest)
            else:
                self.received[token] = [rest]

        # Read all the messages that are waiting (non-blocking read)
        received = False
        while True:
            try:
                print('Receive()')
                parts = self.socket.recv_multipart(flags=zmq.NOBLOCK)
                _handle_parts(parts)
                received = True
            except zmq.ZMQError as e:
                #print('Exception:', e, 'errno', e.errno)
                if e.errno == zmq.EAGAIN:
                    break
                raise e
        if timeout == 0:
            # Don't do a blocking read
            return received

        # Do one blocking read.
        poll = zmq.Poller()
        poll.register(self.socket, zmq.POLLIN)
        events = poll.poll(timeout)
        if len(events):
            parts = self.socket.recv_multipart()
            _handle_parts(parts)
        return received
    
    def wait_for_tokens(self, tokens, timeout=-1):
        '''
        Retrieves results for the given *tokens*, possibly waiting for
        servers to reply.

        Returns a list of result messages, one for each *token*.
        '''
        results = {}
        todo = [k for k in tokens]
        if timeout > 0:
            t0 = time.time()
        while len(todo):
            for k in todo:
                r = self._pop_token(k)
                if r is not None:
                    todo.remove(k)
                    results[k] = r
            if len(todo):
                self._receive(timeout=timeout)
                # adjust timeout
                if timeout > 0:
                    tnow = time.time()
                    timeout = max(0, t0 + timeout - tnow)
                    t0 = tnow
        return [results[k] for k in tokens]
        

if __name__ == '__main__':

    servers = dict(a='tcp://127.0.0.1:5555',
                   b='tcp://127.0.0.1:5556')

    client = RpcClient(servers, identity='client')

    time.sleep(1)

    client.wait_for_setup()

    client.wait_for_setup()
    
    stats = client.get_statistics()
    print('Got stats:', stats)

    R = client.write_chunks([77,78], 0, 0, 'chunk-beam%04i-fpga%012i+%08i.msgpack')
    print('Got:', R)
    
    # server = 'tcp://127.0.0.1:5555'
    # client = RpcClient(server)
    # 
    # import time
    # #time.sleep(1)
    # while True:
    #     print('Try send...')
    #     try:
    #         client.socket.send_multipart([server, 'Hello'])
    #         break
    #     except zmq.error.ZMQError:
    #         pass
    #     time.sleep(0.1)
    #     
    # while True:
    #     msg = client.socket.recv_multipart()
    #     print('Received:', msg)
