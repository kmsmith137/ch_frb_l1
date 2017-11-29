from __future__ import print_function
import zmq
import msgpack
import subprocess

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--address', default='tcp://*:9999')
    parser.add_argument('--port', default=0)
    opt = parser.parse_args()

    if opt.port:
        addr = 'tcp://*:' + str(opt.port)
    else:
        addr = opt.address
    
    context = zmq.Context()
    socket = context.socket(zmq.REP)
    print('Binding', addr)
    socket.bind(addr)

    while True:
        print('Waiting for request...')
        msg = socket.recv()
        print('Received:', msg)

        msg = msgpack.unpackb(msg)
        print('Got', msg)

        reply = 'did not understand request'
        try:
            func = msg[0]
            print('Function:', func)
            func = func.decode()
            if func == 'run':
                cmd = msg[1]
                print('Running command:', cmd)
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
                # timeout=?
                reply = (res.returncode, res.stdout, res.stderr)
                print('Got result:', reply)
                #subprocess.Popen(cmd)
            elif func == 'launch':
                cmd = msg[1]
                print('Launching command:', cmd)
                p = subprocess.Popen(cmd, shell=True, stdin=subprocess.DEVNULL)
                reply = (p.pid)
                print('Got result:', reply)
                #subprocess.Popen(cmd)


        except:
            import traceback
            traceback.print_exc()
        # Send reply
        msg = msgpack.packb(reply)
        socket.send(msg)
