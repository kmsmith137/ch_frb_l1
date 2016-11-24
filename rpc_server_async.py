from __future__ import print_function
import sys
import threading
import time
import random

import zmq
import msgpack

class Worker(threading.Thread):
    def __init__(self, context):
        threading.Thread.__init__(self)
        self.context = context

    def run(self):
        socket = self.context.socket(zmq.DEALER)
        socket.connect('inproc://backend')
        print('Worker started')
        try:
            while True:
                msgs = socket.recv_multipart()
                #print('Worker received request:', msgs)
                client = msgs[0]
                msg = msgs[1]
                
                req = msgpack.unpackb(msg)
                beam, chunk, filename = req
    
                # Do work
                print('Worker writing beam', beam, 'chunk', chunk, '...')
                time.sleep(random.random() * 3)
                
                success,error_message = True, 'Success'
                reply = [beam, chunk, filename, success, error_message]
                print('Worker sending reply...')
                msg = msgpack.packb(reply)
                socket.send_multipart((client, msg))
        except:
            import traceback
            print('Exception in worker:')
            traceback.print_exc()
        socket.close()    

if __name__ == '__main__':
    context = zmq.Context()
    socket = context.socket(zmq.ROUTER)
    socket.setsockopt(zmq.ROUTER_MANDATORY, 1)
    socket.bind('tcp://127.0.0.1:5555')

    # For a server with a (local) pool of workers, see
    #   zmq-guide.html#The-Asynchronous-Client-Server-Pattern
    # ie, http://zguide.zeromq.org/py:asyncsrv

    # Backend socket, for talking to workers.  Note 'inproc' transport.
    backend = context.socket(zmq.DEALER)
    backend.bind('inproc://backend')

    workers = []
    for i in range(2):
        worker = Worker(context)
        worker.start()
        workers.append(worker)

    poll = zmq.Poller()
    poll.register(socket,  zmq.POLLIN)
    poll.register(backend, zmq.POLLIN)
    
    while True:
        #  Wait for next request from client, or reply from Worker
        print('Polling...')
        timeout = None
        events = poll.poll(timeout)

        for s,flag in events:
            if s == backend:
                print('Received result from worker')
                msgs = backend.recv_multipart()
                client = msgs[0]
                msg = msgs[1]
                # Proxy
                socket.send_multipart((client, msg))
                continue

            if s != socket:
                print('Polled socket is neither backend nor frontend:', s)
                continue
            
            # Client message
            print('Received request from client')
            msgs = socket.recv_multipart()
            client = msgs[0]
            msg = msgs[1]
        
            up = msgpack.Unpacker()
            up.feed(msg)
            funcname = up.next()
            print('Function name:', funcname)
            args = up.next()
            print('Args:', args)

            if funcname != 'write_chunks':
                print('Unknown funcname', funcname)
                continue
        
            beams, min_chunk, max_chunk, fn_pat = args
            # For each beam x chunk, send a request to the worker pool.
            for chunk in range(min_chunk, max_chunk):
                print('Sending request to worker...')
                beam, filename = 1, 'filename.msgpack'
                req = msgpack.packb((beam, chunk, filename))
                backend.send_multipart((client, req))

