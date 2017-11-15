# Note: you probably want to run the webapp through the wrapper 
# script 'run-webapp.sh' in the toplevel ch_frb_l1 directory.

from __future__ import print_function

from flask import Flask, render_template, jsonify, request, redirect

import os
import sys
import json
import yaml
import msgpack
import numpy as np

#from flask import Flask, request, session, url_for, redirect, render_template, abort, g, flash, _app_ctx_stack

app = Flask(__name__)

_rpc_client = None
def get_rpc_client():
    global _rpc_client
    if _rpc_client is None:
        from rpc_client import RpcClient
        _rpc_client = RpcClient(dict([(''+str(i), k) for i,k in enumerate(app.nodes)]))
    return _rpc_client

def parse_config():
    """
    The webapp assumes that the WEBAPP_CONFIG environment variable is set to the
    name of a yaml config file.  

    Currently, this file only needs to contain the key 'rpc_address', which should 
    be a list of RPC server locations in the format 'tcp://10.0.0.101:5555'.  Note
    that an l1_config file will probably work, since it contains the 'rpc_address'
    key among others.

    This function parses the yaml file and returns the list of nodes, after some
    sanity checks.
    """

    if not os.environ.has_key('WEBAPP_CONFIG'):
        print("webapp: WEBAPP_CONFIG environment variable not set")
        print("  Maybe you want to run the webapp through the wrapper script")
        print("  'run-webapp.sh' in the toplevel ch_frb_l1 directory, which")
        print("  automatically sets this variable?")
        sys.exit(1)

    config_filename = os.environ['WEBAPP_CONFIG']

    if not os.path.exists(config_filename):
        print("webapp: config file '%s' not found" % config_filename)
        sys.exit(1)

    try:
        y = yaml.load(open(config_filename))
    except:
        print("webapp: couldn't parse yaml config file '%s'" % config_filename)
        sys.exit(1)

    if not isinstance(y,dict) or not y.has_key('rpc_address'):
        print("webapp: no 'rpc_address' field found in yaml file '%s'" % config_filename)
        sys.exit(1)

    nodes = y['rpc_address']

    # allow a single string (not a list)
    if isinstance(nodes,basestring):
        nodes = [nodes]

    if not isinstance(nodes,list) or not all(isinstance(x,basestring) for x in nodes):
        print("%s: expected 'rpc_address' field to be a list of strings" % config_filename)
        sys.exit(1)

    # FIXME(?): check the format of the node strings here?
    # (Should be something like 'tcp://10.0.0.101:5555')

    return nodes

app.nodes = parse_config()

@app.route('/')
def index():
    print('nodes:', app.nodes)
    return render_template('index-newer.html',
                           nodes = app.nodes,
                           enodes = enumerate(app.nodes),
                           node_status_url='/node-status',
                           packet_matrix_url='/packet-matrix',
                           packet_matrix_image_url='/packet-matrix.png',
                           packet_matrix_d3_url='/packet-matrix-d3',
        )

def sort_l0_nodes(senders):
    # Assume that senders are IP:port addresses; drop port
    sender_ips = [s.split(':')[0] for s in senders]
    # Sort numerically
    numip = [sum([int(n) * 1<<(i*8) for i,n in enumerate(reversed(s.split('.')))])
             for s in sender_ips]

    dnsnames = []
    for s in sender_ips:
        # nums = [10,1,7,18]
        nums = [int(ss) for ss in s.split('.')]
        if not(nums[0] == 10 and nums[1] == 1):
            dnsnames.append(s)
            continue
        digit = nums[2]
        north = (digit >= 0 and digit <= 14)
        south = (digit >= 100 and digit <= 114)
        if not(north or south):
            dnsnames.append(s)
            continue
        if north:
            rack = digit
            ns = 'n'
        else:
            ns = 's'
            rack = digit - 100
        pos = nums[3]
        if not(pos >= 10 and pos <= 19):
            dnsnames.append(s)
            continue
        name = 'c%s%xg%i' % (ns, rack, pos)
        dnsnames.append(name)

    numip = [sum([int(n) * 1<<(i*8) for i,n in enumerate(reversed(s.split('.')))])
             for s in sender_ips]

    I = np.argsort(numip)
    senders = [senders[i] for i in I]
    sender_names = [dnsnames[i] for i in I]
    return senders,sender_names

def get_packet_matrix():
    # Send RPC requests to all nodes, gather results into an HTML table
    client = get_rpc_client()
    # Make RPC requests async.  Timeouts are in *milliseconds*.
    timeout = 5000.

    rates = client.get_packet_rate(timeout=timeout)

    senders = set()
    packetrates = []
    for p in rates:
        if p is None:
            packetrates.append({})
            continue
        senders.update(p.packets.keys())
        # Packet counts -> rates
        packetrates.append(dict([(k, v/p.period) for k,v in p.packets.items()]))
        
    senders = list(senders)

    # Parse and sort
    senders,sender_names = sort_l0_nodes(senders)
    return senders, sender_names, packetrates
    
@app.route('/packet-matrix-d3')
def packet_matrix_d3():
    return render_template('packets-d3.html',
                           nodes = app.nodes,
                           enodes = enumerate(app.nodes),
                           packet_matrix_json_url='/packet-matrix.json',)

@app.route('/packets-l0/<name>/<ip>')
def packets_l0(name=None, ip=None):
    return render_template('packets-l0-d3.html',
                           node0name=name,
                           node0ip=ip,
                           nodes1=app.nodes)

@app.route('/packets-l1/<name>')
def packets_l1(name=None):
    return render_template('packets-l1-d3.html',
                           node=name)

@app.route('/packet-rate-l1-json/<name>')
def packet_rate_l1_json(name=None):
    assert(name is not None)
    client = get_rpc_client()
    timeout = 5000.

    rservers = dict([(v,k) for k,v in client.servers.items()])
    servers = [rservers['tcp://' + str(name)]]
    
    graph = client.get_packet_rate_history(start=-600,
                                           servers=servers,
                                           timeout=timeout)
    #print('Got graph:', graph)
    graph = graph[0]

    if graph is None:
        return jsonify({})

    times,rates = graph
    # 'rates' is an array of vectors; we take the first one (the sum)
    rate = rates[0]
    return jsonify(dict(times=times, rates=rate))

@app.route('/packet-rate-l0-json/<ip>')
def packet_rate_l0_json(ip=None):
    assert(ip is not None)
    client = get_rpc_client()
    timeout = 5000.

    print('RPC request for rates of L0 node', ip)
    
    graphs = client.get_packet_rate_history(start=-20,
                                            l0nodes=[ip],
                                            timeout=timeout)

    print('Graphs:', graphs)
    
    ##### Hmmmmm, the graphs are going to have all different times...
    ntotal = len(graphs)
    graphs = [g for g in graphs if g is not None]
    nreplies = len(graphs)

    if len(graphs):
        times,rates = graphs[0]
        rate = rates[0]

        print('Times:', times)
        print('Rates:', rates)

        tt = np.array(times)
        for t,r in graphs[1:]:
            r = r[0]
            ## Sum each rate into the nearest time bin...
            for ti,ri in zip(t,r):
                i = np.argmin(np.abs(tt - ti))
                rate[i] += ri
    else:
        times = []
        rate = []

    return jsonify(dict(times=times, rates=rate,
                        nreplies=nreplies, ntotal=ntotal))

@app.route('/packet-matrix.json')
def packet_matrix_json():
    senders, sender_names, packets = get_packet_matrix()

    # npackets = []
    # for p in packets:
    #     row = []
    #     for s in senders:
    #         row.append(p.get(s, 0))
    #     npackets.append(row)

    # Flat packet list
    npackets = []
    for p in packets:
        for s in senders:
            npackets.append(p.get(s, 0))
    
    rtn = dict(l0=sender_names, l0_ip=senders,
               l1=[n.replace('tcp://','') for n in app.nodes],
               packets=npackets)
    return jsonify(rtn)
    
@app.route('/packet-matrix')
def packet_matrix():
    senders, sender_names, packets = get_packet_matrix()
    
    html = '<html><body>'
    html += '<table><tr><td></td>'
    for i,l0 in enumerate(sender_names):
        html += '<td><a href="l0/%s">%s</a></td>' % (l0,l0)
    html += '</tr>\n'
    for i,p in enumerate(packets):
        html += '<tr><td><a href="host/%s">%i</a></td>' % (app.nodes[i].replace('tcp://',''), i+1)
        for l0 in senders:
            n = p.get(l0, 0)
            html += '<td>%i</td>' % n
        html += '</tr>\n'
    html += '</table>'
    html += '</body></html>'
    return html



@app.route('/xxx')
def index_xxx():

    import requests

    url = 'http://localhost:9090/api/v1/query?query=up{job="chime_frb_l1"}'
    r = requests.get(url)
    assert(r.status_code == 200)
    json = r.json()
    print('UP json:', json)
    assert(json['status'] == 'success')
    data = json['data']
    print('Data:', data)
    assert(data['resultType'] == 'vector')
    data = data['result']
    print('Data:', data)
    up = {}
    for item in data:
        print('  item', item)
        timestamp,val = item['value']
        up[item['metric']['instance']] = val

    print('Up:', up)

    hosts = up.keys()
    hosts.sort()


    url = 'http://localhost:9090/api/v1/query?query=up{job="chime_frb_datacenter"}'
    r = requests.get(url)
    assert(r.status_code == 200)
    json = r.json()
    print('UP json:', json)
    assert(json['status'] == 'success')
    data = json['data']
    print('Data:', data)
    assert(data['resultType'] == 'vector')
    data = data['result']
    print('Data:', data)
    nodeup = {}
    for item in data:
        print('  item', item)
        timestamp,val = item['value']
        nodeup[item['metric']['instance']] = val
    print('Up:', nodeup)
    nodes = nodeup.keys()
    nodes.sort()


    #nodes=list(enumerate(app.nodes)),
    return render_template('index-new.html',
                           nnodes = len(app.nodes),
                           hosts = hosts,
                           ehosts = list(enumerate(hosts)),
                           up = up,

                           nodes = nodes,
                           enodes = list(enumerate(nodes)),
                           nodeup = nodeup,

                           node_status_url='/node-status')


@app.route('/node/<name>')
def node(name=None):
    return redirect('http://localhost:3000/dashboard/db/node-exporter-single-server?orgId=2&from=now-1h&to=now&var-server=%s' % name)



@app.route('/packet-matrix.png')
def packet_matrix_png():
    import pylab as plt
    import numpy as np

    senders, sender_names, packets = get_packet_matrix()
    
    if len(senders) == 0:
        senders = ['null']

    nrecv = len(app.nodes)
    nsend = len(senders)
        
    npackets = np.zeros((nrecv, nsend), int)
    for irecv,p in enumerate(packets):
        for isend,l0 in enumerate(senders):
            npackets[irecv,isend] = p.get(l0, 0)

    from io import BytesIO
    out = BytesIO()
    # plt.clf()
    # plt.imshow(npackets, interpolation='nearest', origin='lower', vmin=0)
    # plt.colorbar()
    # plt.xticks(np.arange(nsend))
    # plt.xlabel('L0 senders')
    # plt.yticks(np.arange(nrecv))
    # plt.ylabel('L1 receivers')
    # plt.savefig(out, format='png')
    # plt.title('Packets received matrix')

    plt.imsave(out, npackets, format='png')

    bb = out.getvalue()

    return (bb, {'Content-type': 'image/png'})

@app.route('/2')
def index2():
    return render_template('index2.html', nodes=list(enumerate(app.nodes)),
                           node_status_url='/node-status')

@app.route('/node-status')
def node_status():
    #    a = request.args.get('a', 0, type=int)
    #j = jsonify([ dict(addr=k) for k in app.nodes ])

    client = get_rpc_client()
    # Make RPC requests for list_chunks and get_statistics asynchronously
    timeout = 5000.

    ltokens = client.list_chunks(wait=False)
    stats = client.get_statistics(timeout=timeout)
    ch = client.wait_for_tokens(ltokens, timeout=timeout)
    ch = [msgpack.unpackb(p[0]) if p is not None else None
          for p in ch]
    
    # print('Chunks:')
    # for bch in ch:
    #     if bch is None:
    #         continue
    #     for b,f0,f1,w in bch:
    #         Nchunk = 1024 * 400
    #         print('  beam', b, 'chunk', f0/Nchunk, '+', (f1-f0)/Nchunk, 'from', w)

    #print('Stats[0]:', stats[0])

    stat = [ dict(addr=k, status='ok', chunks=chi, stats=st) for k,chi,st in zip(app.nodes, ch, stats) ]
    j = json.dumps(stat)
    # print('JSON:', j)
    return j

if __name__ == '__main__':
    app.run()
