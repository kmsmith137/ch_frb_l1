from __future__ import print_function
import sys
import threading
import time
import random

import zmq
import msgpack

'''
A python prototype of how an async version of the RPC server might
work.

One desired property is that the server respond to client requests
quickly, because we don't want requested data to drop out of the ring
buffer while we're servicing some other client's request.  Therefore,
we want the RPC server to have one or more workers that are actually
writing files to disk, and then the RPC server itself just has to poll
on the client socket waiting for requests, and the worker socket
waiting for replies; when it gets one it forwards it to the client.

In C++, if we use shared_ptrs to keep the assembled_chunks alive, then
we need to work a little harder.  The RPC server will receive client
requests and retrieve shared_ptrs for the assembled_chunks to be
written out.  It needs to communicate those shared_ptrs to the worker
threads, in such a way that the shared_ptr stays alive.  Perhaps we
should use a (mutex-protected) std::queue of shared_ptrs (or a struct
including the shared_ptr and other function args) to pass them between
the RPC server and worker threads.  We could still use ZMQ for the
messaging, but the server to worker request would just be an empty
message saying "grab work from the queue"; the reply could stay the
same and go via ZMQ.

Otherwise, the RPC server would have to keep like a map of the
requests to shared_ptrs, removing them when the request completed.

'''


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

            if funcname == 'write_chunks':
                args = up.next()
                print('Args:', args)
                beams, min_chunk, max_chunk, fn_pat = args
                # For each beam x chunk, send a request to the worker pool.
                for chunk in range(min_chunk, max_chunk):
                    print('Sending request to worker...')
                    beam, filename = 1, 'filename.msgpack'
                    req = msgpack.packb((beam, chunk, filename))
                    backend.send_multipart((client, req))

            elif funcname == 'get_statistics':
                reply = msgpack.packb([{'hello':42, 'world':43},{'yo':100},])
                print('Client:', client)
                #socket.send_multipart([client, reply])
                socket.send(client, zmq.SNDMORE);
                socket.send(reply);
            else:
                print('Unknown funcname', funcname)
                continue
        
