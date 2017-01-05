from __future__ import print_function
import zmq
import msgpack
import threading

def client_thread(context, me):
    socket = context.socket(zmq.DEALER)
    socket.set(zmq.IDENTITY, "client%i" % me)
    socket.connect('tcp://localhost:5555')
    
    beams = [76,77,78]
    #minfpga = 0
    #maxfpga = 50000000
    #38502400 to 38912000
    minfpga = 38600000
    maxfpga = 38600000
    priority = 10
    filename_pat = 'chunk-%02llu-chunk%08llu-py.msgpack'
    msg = (msgpack.packb('write_chunks') +
           msgpack.packb([beams, minfpga, maxfpga, filename_pat, priority]))
    print('Client', me, ': sending request...')
    socket.send(msg)

    beams = [77]
    priority = 20
    filename_pat = 'chunk-%02llu-chunk%08llu-py.msgpack'
    msg = (msgpack.packb('write_chunks') +
           msgpack.packb([beams, minfpga, maxfpga, filename_pat, priority]))
    print('Client', me, ': sending request...')
    socket.send(msg)
    
    while True:
        msg = socket.recv()
        #print('Client', me, ': Received reply: %i bytes' % len(msg))
        # print('Message:', repr(msg))
        rep = msgpack.unpackb(msg)
        print('Client', me, ': got reply:', rep)
    socket.close()

if __name__ == '__main__':
    context = zmq.Context()

    t1 = threading.Thread(target=client_thread, args=(context,1))
    t2 = threading.Thread(target=client_thread, args=(context,2))
    t1.daemon = True
    t2.daemon = True
    t1.start()
    #t2.start()

    from time import sleep
    sleep(10)
    import sys
    print('Quitting')
    sys.exit(0)
    
    t1.join()
    t2.join()
    context.term()
    
