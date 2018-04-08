from __future__ import print_function

import sys
import time
import yaml
import json
from datetime import datetime
from rpc_client import RpcClient

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', default='l1_configs/l1_production_8beam_webapp.yaml',
                        help='YAML config file listing L1 server RPC addresses')
    parser.add_argument('--period', default=300, type=float, help='Time to wait between samples, in seconds')
    parser.add_argument('--output', default='packet-rate-%04i.json', help='Packet rate output filename pattern')
    args = parser.parse_args()

    config_fn = args.config
    try:
        y = yaml.load(open(config_fn))
    except:
        print("Failed to parse yaml config file '%s'" % config_fn)
        raise
        
    if not isinstance(y,dict) or not 'rpc_address' in y:
        print("No 'rpc_address' field found in yaml file '%s'" % config_fn)
        sys.exit(1)

    nodes = y['rpc_address']
    # allow a single string (not a list)
    if isinstance(nodes, basestring):
        nodes = [nodes]
    if not isinstance(nodes,list) or not all(isinstance(x, basestring) for x in nodes):
        print("%s: expected 'rpc_address' field to be a list of strings" % config_fn)
        sys.exit(1)

    client = RpcClient(dict([(''+str(i), k) for i,k in enumerate(nodes)]))

    step = 0
    t0 = time.time()
    while True:

        start_time = t0 - args.period
        end_time = t0
        rates = client.get_packet_rate(start=start_time, period=args.period, timeout=args.period)
        #print('Packet rates:', rates)

        # Find the set of all L0 senders.
        senders = set()
        for node,rate in zip(nodes, rates):
            if rate is None:
                continue
            senders.update(rate.packets.keys())
        senders = list(senders)
        # Decide on the order of senders in the matrix
        senders.sort()
        senderindex = dict([(s,i) for i,s in enumerate(senders)])

        # Build the matrix
        matrix = [[0 for s in senders] for n in nodes]
        for i,rate in enumerate(rates):
            if rate is None:
                continue
            period = rate.period
            for sender,packets in rate.packets.items():
                #matrix[i][senderindex[sender]] = float(packets) / float(period)
                # Save about 3 digits
                matrix[i][senderindex[sender]] = 0.001 * int((1000. * packets) / float(period))
        #print('Matrix:', matrix)
        output = (datetime.now().isoformat(), nodes, senders, matrix)
        
        outfn = args.output % step
        print('Writing to file', outfn)
        json.dump(output, open(outfn, 'w'))
        step += 1
        
        tnow = time.time()
        tsleep = t0 + args.period - tnow
        print('Sleeping', tsleep)
        time.sleep(tsleep)
        t0 += args.period



if __name__ == '__main__':
    main()

