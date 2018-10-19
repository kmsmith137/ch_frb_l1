from __future__ import print_function
import time
import subprocess
import select

class CncSsh(object):
    def __init__(self, ssh_options=None):
        self.ssh_options = ssh_options

    def run(self, command, servers, timeout=1000, launch=False, captive=False):
        procs = []
        for i,server in enumerate(servers):
            if '__INDEX__' in command:
                cmd = command.replace('__INDEX__', str(i))
            else:
                cmd = command
            cmd = "ssh %s %s '%s'" % (self.ssh_options or '', server, cmd)
            print('Command:', cmd)
            procs.append(subprocess.Popen(cmd, shell=True,
                                          stdout=subprocess.PIPE,
                                          stderr=subprocess.STDOUT))
        t0 = time.time()
        returns = [None for s in servers]
        outputs = ['' for s in servers]
        while True:
            alldone = True
            for i,p in enumerate(procs):
                if p is None:
                    continue
                r = p.poll()
                print('Server', i, 'returned:', r)
                if r is None:
                    alldone = False
                else:
                    returns[i] = r
                    outputs[i] += p.stdout.read() # + p.stderr.read()
                    procs[i] = None
            if alldone:
                break

            # rlist = []
            # rmap = {}
            # for i,p in enumerate(procs):
            #     if p is None:
            #         continue
            #     rlist.append(procs[i].stdout)
            #     rmap[procs[i].stdout] = i
            # rlist,nil,nil = select.select(rlist, [], [], 0.)
            # for r in rlist:
            #     i = rmap[r]
            #     #print('Ready:', r, type(r))
            #     #data = r.read1(1024)
            #     data = r.read(1024)
            #     outputs[i] += data
            #     print('Read:', data)

            dt = (time.time() - t0)*1000
            print('Time taken:', dt, 'vs timeout', timeout)
            if dt > timeout:
                print('timeout')
                break

            time.sleep(1.)

        rtn = []
        for r,o in zip(returns, outputs):
            if r is None:
                rtn.append(None)
            else:
                rtn.append((r, o, ''))
        return rtn


if __name__ == '__main__':
    client = CncSsh()
    servers = ['cf0g0', 'cf0g1', 'cf0g2']
    command = 'echo "Hello from server __INDEX__, I am $(hostname)"; sleep 2; echo "Bye"'
    res = client.run(command, servers, timeout=3000)
    print('Results:', res)
