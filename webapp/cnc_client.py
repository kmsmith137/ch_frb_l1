from __future__ import print_function
import zmq
import msgpack
import time

class CncClient(object):
    def __init__(self, ctx=None):
        if ctx is not None:
            self.ctx = ctx
        else:
            self.ctx = zmq.Context()

    def run(self, command, servers, timeout=1000, launch=False):
        # Create one socket per server
        sockets = [self.ctx.socket(zmq.REQ) for s in servers]
        # Send the request
        runcmd = 'run'
        if launch:
            runcmd = 'launch'
        for i,(socket,server) in enumerate(zip(sockets, servers)):
            if '__INDEX__' in command:
                cmd = command.replace('__INDEX__', str(i))
            else:
                cmd = command
            msg = msgpack.packb([runcmd, cmd])
            socket.connect(server)
            socket.send(msg)

        poll = zmq.Poller()
        todo = sockets
        for socket in todo:
            poll.register(socket, zmq.POLLIN)
        t0 = time.time()
        results = dict([(socket, None) for socket in sockets])
        while True:
            print('Polling with timeout', timeout)
            print('poll.sockets:', poll.sockets)
            events = poll.poll(timeout=timeout)
            if len(events) == 0:
                # timeout
                print('Poll timed out')
                break
            for socket,event in events:
                print('Reading from socket', sockets.index(socket))
                reply = socket.recv()
                msg = msgpack.unpackb(reply)
                #print('Got result:', msg)
                results[socket] = msg
                poll.unregister(socket)
            print('poll.sockets:', poll.sockets)
            if len(poll.sockets) == 0:
                # all done
                break
            t1 = time.time()
            timeout -= 1000. * (t1 - t0)
            t0 = t1
        rtn = [results[s] for s in sockets]
        #print('Returning:', rtn)
        for s in sockets:
            s.close()
        return rtn

    # def close(self):
    #     if self.ctx is not None:
    #         self.ctx.destroy()
    #         self.ctx = None

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--address', default='tcp://127.0.0.1:9999')
    opt = parser.parse_args()

    context = zmq.Context()
    socket = context.socket(zmq.REQ)

    #msg = msgpack.packb(['hello', 'world'])
    msg = msgpack.packb(['run', 'echo "hello world"'])
    print('Message:', msg)
    socket.connect(opt.address)
    print('Sending to', opt.address)
    socket.send(msg)
    print('Waiting for reply...')
    reply = socket.recv()
    print('Got reply:', reply)
    msg = msgpack.unpackb(reply)
    print('Reply:', msg)
    
    msg = msgpack.packb(['launch', './ch-frb-l1 -t l1_configs/l1_example1.yaml'])
    print('Message:', msg)
    socket.connect(opt.address)
    print('Sending to', opt.address)
    socket.send(msg)
    print('Waiting for reply...')
    reply = socket.recv()
    print('Got reply:', reply)
    msg = msgpack.unpackb(reply)
    print('Reply:', msg)
    
