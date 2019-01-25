from __future__ import print_function
import yaml
from rpc_client import RpcClient

if __name__ == '__main__':
    y = yaml.load(open('l1_configs/l1_production_8beam_webapp.yaml'))
    #nodes = y['rpc_address']
    #print('Nodes:', nodes)
    #nodes = nodes[:3]

    nodes = ['tcp://10.6.201.10:5555']

    servers = dict([(n,n) for n in nodes])

    client = RpcClient(servers)

    fpgas = client.get_max_fpga_counts(timeout=5.)

    print('Got:', fpgas)

    for i,fpga in enumerate(fpgas):
        print()
        #print(i, fpga)
        for where,beam,f in fpga:
            if where == 'bonsai':
                print('FPGA', f)
                print((f / 384) % 4096)
        
