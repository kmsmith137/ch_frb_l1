from __future__ import print_function
import zmq
import msgpack
import subprocess
from threading  import Thread
import sys

try:
    # py2
    from Queue import Queue, Empty
except ImportError:
    # py3
    from queue import Queue, Empty

# Apparently, there's not a really simple clean way of doing
# non-blocking reads from subprocess PIPEs, so we launch a thread for
# each pipe and jam lines into a queue.
def enqueue_output(out, queue):
    for line in iter(out.readline, b''):
        #print('Read from pipe:', line)
        queue.put(line)
    out.close()

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

    # processes we have launched
    launched_procs = []

    # pid -> (proc, stdoutq, stderrq)
    captive_procs = {}

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
                proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, executable='/bin/bash', close_fds=True)
                (stdout,stderr) = proc.communicate()
                reply = (proc.returncode, stdout, stderr)
                print('Got result:', reply)
            elif func == 'launch':
                cmd = msg[1]
                captive = False
                if len(msg) > 2:
                    captive = msg[2]
                print('Launching command:', cmd, 'captive?', captive)
                #p = subprocess.Popen(cmd, shell=True, stdin=subprocess.DEVNULL)
                if not captive:
                    p = subprocess.Popen(cmd, shell=True, executable='/bin/bash', close_fds=True)
                    launched_procs.append(p)
                else:
                    print('Running command:', cmd)
                    p = subprocess.Popen(cmd, shell=True, executable='/bin/bash', close_fds=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    qout = Queue()
                    t = Thread(target=enqueue_output, args=(p.stdout, qout))
                    t.daemon = True
                    t.start()
                    qerr = Queue()
                    t = Thread(target=enqueue_output, args=(p.stderr, qerr))
                    t.daemon = True
                    t.start()
                    captive_procs[p.pid] = (p, qout, qerr)
                    
                reply = (0, p.pid, '')
                print('Got result:', reply)

            elif func == 'read_proc':
                pid = msg[1]
                if not pid in captive_procs:
                    print('PID', pid, 'not in captive processes')
                    print('keys:', captive_procs.keys())
                    reply = (None, None, None)
                else:
                    (proc, qout, qerr) = captive_procs[pid]
                    # done yet?  set returncode
                    proc.poll()
                    out = []
                    try:
                        while True:
                            s = qout.get_nowait()
                            out.append(s)
                    except Empty:
                        pass
                    #print('Got from queue:', out)
                    out = '\n'.join(out)
                    err = []
                    try:
                        while True:
                            s = qout.get_nowait()
                            err.append(s)
                    except Empty:
                        pass
                    #print('Got from err queue:', err)
                    err = '\n'.join(err)
                    reply = (proc.returncode, out, err)

            elif func == 'quit':
                print('Quitting!')
                sys.exit(0)
        except:
            import traceback
            traceback.print_exc()
        # Send reply
        msg = msgpack.packb(reply)
        socket.send(msg)
