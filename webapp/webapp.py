from __future__ import print_function
import json
from flask import Flask, render_template, jsonify, request

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

@app.route('/node-status')
def node_status():
    #    a = request.args.get('a', 0, type=int)
    #j = jsonify([ dict(addr=k) for k in app.nodes ])

    global client
    if client is None:
        client = RpcClient(dict([(''+str(i), k) for i,k in enumerate(app.nodes)]))

    ch = client.list_chunks(timeout=3.)
    print('Chunks:', ch)

    stat = [ dict(addr=k, status='ok', chunks=chi) for k,chi in zip(app.nodes, ch) ]
    j = json.dumps(stat)
    print('Status:', j)
    return j

if __name__ == '__main__':
    app.run()
