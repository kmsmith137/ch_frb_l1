from __future__ import print_function
import json
from flask import Flask, render_template, jsonify, request
import msgpack

from rpc_client import RpcClient

#from flask import Flask, request, session, url_for, redirect, render_template, abort, g, flash, _app_ctx_stack

app = Flask(__name__)

# L1 RPC nodes
app.nodes = ['tcp://127.0.0.1:5555',
             ]

client = None

@app.route('/')
def index():
    return render_template('index.html', nodes=list(enumerate(app.nodes)),
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

    #ch = client.list_chunks(timeout=3.)

    ### HACK -- no timeouts here -- will wait forever!
    # Make RPC requests for list_chunks and get_statistics asynchronously
    ltokens = client.list_chunks(wait=False)
    stats = client.get_statistics()
    ch = client.wait_for_tokens(ltokens)
    ch = [msgpack.unpackb(p[0]) if p is not None else None
          for p in ch]
    
    # print('Chunks:')
    # for bch in ch:
    #     if bch is None:
    #         continue
    #     for b,f0,f1,w in bch:
    #         Nchunk = 1024 * 400
    #         print('  beam', b, 'chunk', f0/Nchunk, '+', (f1-f0)/Nchunk, 'from', w)

    #print('Stats:', stats)
    
    stat = [ dict(addr=k, status='ok', chunks=chi, stats=st) for k,chi,st in zip(app.nodes, ch, stats) ]
    j = json.dumps(stat)
    # print('JSON:', j)
    return j

if __name__ == '__main__':
    app.run()
