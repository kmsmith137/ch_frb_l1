# Note: you probably want to run the webapp through the wrapper 
# script 'run-webapp.sh' in the toplevel ch_frb_l1 directory.

from __future__ import print_function

from flask import Flask, render_template, jsonify, request

import os
import sys
import json
import yaml
import msgpack

from rpc_client import RpcClient

#from flask import Flask, request, session, url_for, redirect, render_template, abort, g, flash, _app_ctx_stack

app = Flask(__name__)


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

client = None

@app.route('/')
def index():
    return render_template('index.html', nodes=list(enumerate(app.nodes)),
                           nnodes = len(app.nodes),
                           node_status_url='/node-status')

@app.route('/2')
def index2():
    return render_template('index2.html', nodes=list(enumerate(app.nodes)),
                           node_status_url='/node-status')

@app.route('/node-status')
def node_status():
    #    a = request.args.get('a', 0, type=int)
    #j = jsonify([ dict(addr=k) for k in app.nodes ])

    global client
    if client is None:
        client = RpcClient(dict([(''+str(i), k) for i,k in enumerate(app.nodes)]))

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
