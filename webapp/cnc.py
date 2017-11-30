from __future__ import print_function
import zmq
import msgpack

# try:
#     # python2
#     import subprocess32 as subprocess
# except:
#     import subprocess
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

    # don't know if this is necessary... launched process handles
    launched_procs = []

    # use a poller with timeout to periodically check whether
    # launched_procs have terminated...
    poll = zmq.Poller()
    poll.register(socket, zmq.POLLIN)

    while True:
        print('Waiting for request...')
        events = poll.poll(timeout=5000)
        if len(events) == 0:
            # timed out
            for p in launched_procs:
                print('Checking child pid', p.pid)
                if p.poll() is not None:
                    print('Child PID', p.pid, 'terminated with', p.returncode)
                    launched_procs.remove(p)
            continue
        msg = socket.recv()
        print('Received:', msg)

        msg = msgpack.unpackb(msg)
        print('Got', msg)

        reply = (-1000, '', 'did not understand request')
        try:
            func = msg[0]
            print('Function:', func)
            func = func.decode()
            if func == 'run':
                cmd = msg[1]
                print('Running command:', cmd)
                #res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
                # timeout=?
                #reply = (res.returncode, res.stdout, res.stderr)
                proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, close_fds=True)
                (stdout,stderr) = proc.communicate()
                reply = (proc.returncode, stdout, stderr)
                print('Got result:', reply)
            elif func == 'launch':
                cmd = msg[1]
                print('Launching command:', cmd)
                #p = subprocess.Popen(cmd, shell=True, stdin=subprocess.DEVNULL)
                p = subprocess.Popen(cmd, shell=True, close_fds=True)
                launched_procs.append(p)
                reply = (0, 'Started pid %i' % p.pid, '')
                print('Got result:', reply)

        except:
            import traceback
            traceback.print_exc()
        # Send reply
        msg = msgpack.packb(reply)
        socket.send(msg)
