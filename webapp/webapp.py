# Note: you probably want to run the webapp through the wrapper 
# script 'run-webapp.sh' in the toplevel ch_frb_l1 directory.

from __future__ import print_function

from flask import Flask, render_template, jsonify, request

import os
import sys
import json
import yaml
import msgpack

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

    if not isinstance(nodes,list) or not all(isinstance(x,basestring) for x in nodes):
        print("%s: expected 'rpc_address' field to be a list of strings" % config_filename)
        sys.exit(1)

    # FIXME(?): sanity-check the format of the node strings here?
    # (Should be something like 'tcp://10.0.0.101:5555'

    return nodes


app.nodes = parse_config()

@app.route('/')
def index():

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

@app.route('/packet-matrix')
def packet_matrix():
    # Send RPC requests to all nodes, gather results into an HTML table
    client = get_rpc_client()
    # Make RPC requests for list_chunks and get_statistics asynchronously
    timeout = 5.

    allstats = client.get_statistics(timeout=timeout)
    print('Stats:', allstats)
    packets = [s[1] for s in allstats]
    print('Packet stats:', packets)

    senders = set()
    for p in packets:
        senders.update(p.keys())
    senders = list(senders)
    senders.sort()

    html = '<html><body>'
    html += '<table><tr><td></td>'
    for i,l0 in enumerate(senders):
        html += '<td><a href="l0/%s"> </a></td>' % l0
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

@app.route('/packet-matrix.png')
def packet_matrix_png():
    import pylab as plt
    import numpy as np
    
    # Send RPC requests to all nodes, gather results into a plot
    client = get_rpc_client()
    # Make RPC requests for list_chunks and get_statistics asynchronously
    timeout = 5.

    allstats = client.get_statistics(timeout=timeout)
    packets = [s[1] for s in allstats]

    senders = set()
    for p in packets:
        senders.update(p.keys())
    senders = list(senders)
    senders.sort()

    if len(senders) == 0:
        senders = ['null']
    
    npackets = np.zeros((len(app.nodes), len(senders)), int)
    for i,p in enumerate(packets):
        for j,l0 in enumerate(senders):
            n = p.get(l0, 0)
            npackets[i,j] = n

    from io import BytesIO
    out = BytesIO()
    plt.clf()
    plt.imshow(npackets, interpolation='nearest', origin='lower', vmin=0)
    plt.colorbar()
    plt.savefig(out, format='png')
    #plt.imsave(out, npackets, format='png')
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
    timeout = 5.

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
