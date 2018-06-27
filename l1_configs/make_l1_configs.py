from __future__ import print_function

import yaml

def update_beam_ids(info, beam_offset):
    info['beam_ids'] = range(beam_offset, beam_offset+8)
    return info

def update_output_devices(info, rack):
    if rack in ['0','2','4','6','8','A','C']:
        info['output_devices'] = ["/local", "/frb-archiver-1", "/frb-archiver-2"]
    else:
        info['output_devices'] = ["/local", "/frb-archiver-3", "/frb-archiver-4"]
    return info

def update_enos(info, rack):
    if rack in ['0','2','4','6','8','A','C']:
        info['ipaddr'] = ["eno3", "eno4"]
        info['rpc_address'] = ["tcp://eno3:5555", "tcp://eno4:5555"]
        info['prometheus_address'] = ["eno3:8888", "eno4:8888"]
    else:
        info['ipaddr'] = ["eno1", "eno2"]
        info['rpc_address'] = ["tcp://eno1:5555", "tcp://eno2:5555"]
        info['prometheus_address'] = ["eno1:8888", "eno2:8888"]
    return info

def main():
    info = {
        'nbeams': 8,
        'nfreq': 16384,
        'nt_per_packet': 16,
        'beam_ids': [0, 1, 2, 3, 4, 5, 6, 7],
        'ipaddr': [ "eno1",
                  "eno2" ],
        'port': 1313,
        'rpc_address': [ "tcp://eno1:5555", 
        	       "tcp://eno2:5555" ],
        'prometheus_address': [ "eno1:8888",
                              "eno2:8888" ],
        'logger_address': "tcp://10.6.200.19:5555",
        'output_devices': [ "/local", "/frb-archiver-1", "/frb-archiver-2"],
        'slow_kernels': False,
        'assembled_ringbuf_nsamples': 10000,
        'telescoping_ringbuf_nsamples': [ 60000, 120000, 240000 ], # Currently half of what it should be.
        'write_staging_area_gb': 20.0,   # increase to 29 if using bonsai config with _noups_.
        'l1b_executable_filename': "../ch_frb_L1b/ch-frb-l1b.py",
        'l1b_buffer_nsamples': 4000,
        'l1b_pipe_timeout': 0,
        }
    l1_status = yaml.load(open('l1_node_status.yaml'))
    racks = l1_status.keys()
    racks.sort()
    node_status = []
    for rack in racks:
        nodes = l1_status[rack].keys()
        nodes.sort()
        for node in nodes:
            node_status.append(l1_status[rack][node])
    beam_offset = 0
    for i in range(len(node_status)):
        node_info = info
        rack = racks[i/10][4]
        status = node_status[i]
        if status:
            node = i%10
            node_info = update_beam_ids(node_info, beam_offset)
            beam_offset +=8
            #node_info = update_enos(node_info, rack) ### Currently even 10.8 and 10.9 subnets are on eno1/eno2.
            node_info = update_output_devices(node_info, rack) ### Currently even 10.8 and 10.9 subnets are on eno1/eno2.
            with open ("l1_production_8beam_rack%s_node%s.yaml"%(rack.lower(), node), "w") as outfile:
                yaml.dump(node_info, outfile)

if __name__ == "__main__":
    main()
