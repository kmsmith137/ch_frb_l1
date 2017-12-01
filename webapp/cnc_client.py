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

    def quit(self, servers):
        return self.send_with_timeout([['quit']] * len(servers), servers, timeout=1000)
        
    def kill(self, pidmap, timeout=1000):
        # pidmap: node->pid map
        messages = [['kill', v] for v in pidmap.values()]
        servers = pidmap.keys()
        reply = self.send_with_timeout(messages, servers, timeout=timeout)
        return reply

    def run(self, command, servers, timeout=1000, launch=False, captive=False):
        # Compose the request messages
        messages = []
        runcmd = 'run'
        if launch:
            runcmd = 'launch'
        for i in range(len(servers)):
            if '__INDEX__' in command:
                cmd = command.replace('__INDEX__', str(i))
            else:
                cmd = command
            msg = [runcmd, cmd]
            if launch and captive:
                msg.append(True)
            messages.append(msg)
        # Send
        reply = self.send_with_timeout(messages, servers, timeout=timeout)
        return reply

    def poll(self, pid, server, timeout=1000):
        socket = self.ctx.socket(zmq.REQ)
        socket.connect(server)
        msg = msgpack.packb(['read_proc', pid])
        socket.send(msg)
        poll = zmq.Poller()
        poll.register(socket, zmq.POLLIN)
        events = poll.poll(timeout=timeout)
        if len(events):
            reply = socket.recv()
            msg = msgpack.unpackb(reply)
        else:
            msg = (None, None, 'timed out')
        socket.close()
        return msg

    def send_with_timeout(self, messages, servers, timeout=1000):
        # Create one socket per server
        sockets = [self.ctx.socket(zmq.REQ) for s in servers]
        # Send messages
        for i,(socket,server,msg) in enumerate(zip(sockets, servers, messages)):
            socket.connect(server)
            socket.send(msgpack.packb(msg))
        # Create poller for replies
        poll = zmq.Poller()
        for socket in sockets:
            poll.register(socket, zmq.POLLIN)
        # Wait for replies
        t0 = time.time()
        results = dict([(socket, None) for socket in sockets])
        while True:
            #print('Polling with timeout', timeout, 'for', len(poll.sockets), 'sockets')
            #print('poll.sockets:', poll.sockets)
            events = poll.poll(timeout=timeout)
            if len(events) == 0:
                # timeout
                #print('Poll timed out')
                break
            for socket,event in events:
                #print('Reading from socket', sockets.index(socket))
                reply = socket.recv()
                msg = msgpack.unpackb(reply)
                #print('Got result:', msg)
                results[socket] = msg
                poll.unregister(socket)
            #print('poll.sockets:', poll.sockets)
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

if __name__ == '__main__':
    import yaml
    import argparse
    import sys

    parser = argparse.ArgumentParser()
    parser.add_argument('--address', default='tcp://127.0.0.1:9999')
    parser.add_argument('--config', default=None, help='YAML file with rpc_address')
    opt = parser.parse_args()

    context = zmq.Context()

    if opt.config is not None:
        y = yaml.load(open(opt.config))
        servers = y['cnc_address']
        client = CncClient(context)
        print('Sending "quit" message to', servers)
        print('Reply:', client.quit(servers))
        sys.exit(0)

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
    
