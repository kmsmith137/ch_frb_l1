from __future__ import print_function
import zmq
import msgpack

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
    
