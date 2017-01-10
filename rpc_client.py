from __future__ import print_function
import zmq
import msgpack
import time

'''
Python client for the L1 RPC service.

The RPC service is fully asynchronous; each request sends an integer
"token", and replies to that query include the token.  Thus it is
possible to send multiple requests and then later retrieve the
results, or wait synchronously.
'''

class RpcClient(object):
    def __init__(self, servers, context=None, identity=None):
        '''
        *servers*: a dict of [key, address] entries, where each
        *address* is a string address of an RPC server: eg,
        'tcp://localhost:5555'.

        *context*: if non-None: the ZeroMQ context in which to create my socket.

        *identity*: if non-None: the name to give our ZeroMQ socket (string).
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
            self.socket.connect(v)
            # Send initial message to server to establish routing table
            #self.socket.send_multipart([v])
        self.token = 0

        self.received = {}
        
        # self.timeout ??
        
    def wait_for_setup(self, wait=0.1):
        todo = servers.keys()
        while len(todo):
            k = todo[0]
            try:
                print('Trying to send:', k)
                self.socket.send_multipart([self.servers[k]])
                todo.remove(k)
            except zmq.error.ZMQError:
                pass
            if len(todo):
                time.sleep(wait)

    def get_statistics(self, servers=None, wait=True):
        if servers is None:
            servers = self.servers.keys()
        tokens = []
        for k in servers:
            self.token += 1
            req = msgpack.packb(['get_statistics', self.token])
            tokens.append(self.token)
            self.socket.send_multipart([self.servers[k], req])
        if not wait:
            return tokens
        parts = self.wait_for_tokens(tokens)
        print('Received parts:', parts)
        return [msgpack.unpackb(p[0]) for p in parts]
        
    def pop_token(self, k, d=None):
        return self.received.pop(k, d)

    ## FIXME -- allow timeout -- poll().
    def receive(self, block=True):
        def _handle_parts(parts):
            server = parts[0]
            hdr  = parts[1]
            token = msgpack.unpackb(hdr)
            print('Received token:', token)
            rest = parts[2:]
            assert(token not in self.received)
            self.received[token] = rest

        while True:
            try:
                print('Receive()')
                parts = self.socket.recv_multipart(flags=zmq.NOBLOCK)
                _handle_parts(parts)
            except zmq.ZMQError as e:
                #print('Exception:', e, 'errno', e.errno)
                if e.errno == zmq.EAGAIN:
                    break
                raise e
        # Block once
        parts = self.socket.recv_multipart()
        _handle_parts(parts)
    
    def wait_for_tokens(self, tokens):
        results = {}
        todo = [k for k in tokens]
        while len(todo):
            for k in todo:
                r = self.pop_token(k)
                if r is not None:
                    todo.remove(k)
                    results[k] = r
            if len(todo):
                self.receive()
        return [results[k] for k in tokens]
        

if __name__ == '__main__':

    servers = dict(a='tcp://127.0.0.1:5555',
                   b='tcp://127.0.0.1:5556')

    client = RpcClient(servers)

    client.wait_for_setup()

    stats = client.get_statistics()
    print('Got stats:', stats)
    
    
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
