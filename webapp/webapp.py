# Note: you probably want to run the webapp through the wrapper 
# script 'run-webapp.sh' in the toplevel ch_frb_l1 directory.

from __future__ import print_function

try:
    basestring
except:
    # py3
    basestring = str

from flask import Flask, render_template, jsonify, request, redirect

import os
import sys
from datetime import datetime, timedelta
import json
import yaml
import msgpack
import numpy as np
import sqlalchemy
from sqlalchemy.orm import sessionmaker, scoped_session
from chlog_database import LogMessage

#print('paths:', '\n'.join(sys.path))
#import webapp
#print('webapp', webapp)

app = Flask(__name__)

_rpc_client = None
def get_rpc_client():
    global _rpc_client
    if _rpc_client is None:
        from rpc_client import RpcClient
        _rpc_client = RpcClient(dict([(''+str(i), k) for i,k in enumerate(app.nodes)]),
                                identity='webapp.py')
    return _rpc_client

def get_cnc_client():
    # from webapp.cnc_client import CncClient
    from cnc_client import CncClient
    client = CncClient(ctx=app.zmq)
    return client

def get_db_session():
    return app.make_db_session()

def parse_config(app):
    """
    The webapp assumes that the WEBAPP_CONFIG environment variables
    are set to the names of a yaml config file.

    Currently, the config file only needs to contain the key
    'rpc_address', which should be a list of RPC server locations in
    the format 'tcp://10.0.0.101:5555'.  Note that an l1_config file
    will probably work, since it contains the 'rpc_address' key among
    others.

    This function parses the yaml file and returns the list of nodes, after some
    sanity checks.
    """

    if 'WEBAPP_CONFIG' not in os.environ:
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

    if not isinstance(y,dict) or not 'rpc_address' in y:
        print("webapp: no 'rpc_address' field found in yaml file '%s'" % config_filename)
        sys.exit(1)

    nodes = y['rpc_address']
    # allow a single string (not a list)
    if isinstance(nodes, basestring):
        nodes = [nodes]
    if not isinstance(nodes,list) or not all(isinstance(x, basestring) for x in nodes):
        print("%s: expected 'rpc_address' field to be a list of strings" % config_filename)
        sys.exit(1)
    app.nodes = nodes
        
    if not 'cnc_address' in y:
        print('No cnc_address item in YAML file; not sending command-n-control')
        cnc_nodes = []
    else:
        cnc_nodes = y['cnc_address']
        # allow a single string (not a list)
        if isinstance(cnc_nodes, basestring):
            cnc_nodes = [cnc_nodes]
        if not isinstance(cnc_nodes,list) or not all(isinstance(x, basestring) for x in cnc_nodes):
            print("%s: expected 'cnc_address' field to be a list of strings" % config_filename)
            sys.exit(1)
    app.cnc_nodes = cnc_nodes
    # FIXME(?): check the format of the node strings here?
    # (Should be something like 'tcp://10.0.0.101:5555')

    database = y.get('log_database', 'sqlite:///log.sqlite3')
    engine = sqlalchemy.create_engine(database)
    app.make_db_session = scoped_session(sessionmaker(bind=engine))

parse_config(app)

import zmq
app.zmq = zmq.Context()

@app.route('/')
def index():
    nodes = [n.replace('tcp://','') for n in app.nodes]
    return render_template('index-newer.html',
                           nodes = nodes,
                           enodes = list(enumerate(nodes)),
                           ecnodes = list(enumerate(app.cnc_nodes)),
                           node_status_url='/node-status',
                           packet_matrix_url='/packet-matrix',
                           packet_matrix_image_url='/packet-matrix.png',
                           packet_matrix_d3_url='/packet-matrix-d3',
                           l0_node_map_url='/l0-node-map',
                           cnc_run_url='/cnc-run',
                           cnc_follow_url='/cnc-poll',
                           cnc_kill_url='/cnc-kill',
        )

@app.route('/l1-logs-stdout')
def l1_logs_stdout():
    # Retrieve systemd/journalctl logs for ch-frb-l1 systemd processes.
    client = get_cnc_client()
    results = client.run('journalctl --system --boot --since yesterday --unit ch-frb-l1', app.cnc_nodes,
                         timeout=3000)
    rr = []
    for r in results:
        print('  ', r)
        if r is None:
            rr.append(None)
        else:
            (rtn, out, err) = r
            out = out.decode()
            err = err.decode()
            rr.append((rtn, out, err))
    results = rr
    print('Results:', results)
    print('CNC nodes:', app.cnc_nodes)
    #return jsonify(results)

    logmsgs = []
    for r in results:
        if r is None:
            logmsgs.append(['Timed out'])
            continue
        rtn,out,err = r
        if rtn != 0:
            logmsgs.append(['Return value: %i' % rtn] + err.split('\n') + out.split('\n'))
        else:
            logmsgs.append(err.split('\n') + out.split('\n'))

    return render_template('l1-logs-stdout.html',
                           logmsgs = list(zip(app.cnc_nodes, logmsgs)),
                       )

@app.route('/l1-logs-recent')
def l1_logs_recent():
    session = get_db_session()
    datecut = datetime.now() - timedelta(0, 60)
    logs = session.query(LogMessage).filter(LogMessage.date >= datecut)
    filters = [('date after ' + str(datecut), 'date_gt_%s' % str(datecut).replace(' ','_'))]
    return render_template('l1-logs-recent.html',
                           logs=logs,
                           logfilters=filters)

@app.route('/l0-node-map')
def l0_node_map():
    return render_template('l0-node-map.html',
                           packet_matrix_json_url='/packet-matrix.json',
                           racks=list(enumerate(['%x' % x for x in range(15)])),
                           nodesperrack=[0,1,2,3,4,5,6,7,8,9])

@app.route('/cnc-kill', methods=['POST'])
def cnc_kill():
    if request.method != 'POST':
        return 'POST only'
    pids = request.get_json()
    print('CNC_kill:', pids)
    pids = dict(pids)
    client = get_cnc_client()
    results = client.kill(pids, timeout=3000)
    return jsonify(results)

@app.route('/cnc-run', methods=['POST',
                                # debug
                                'GET'])
def cnc_run():
    if request.method == 'POST':
        cmd = request.form['cmd']
        launch = request.form.get('launch', False)
        captive = request.form.get('captive', False)
    else:
        cmd = request.args.get('cmd')
        launch = request.args.get('launch', False)
        captive = request.args.get('captive', False)
    print('Launch', launch, 'Captive', captive, 'Command:', cmd)
    client = get_cnc_client()
    results = client.run(cmd, app.cnc_nodes, timeout=5000, launch=launch,
                         captive=captive)
    print('Got results:')
    rr = []
    for r in results:
        print('  ', r)
        if r is None:
            rr.append(None)
        else:
            (rtn, out, err) = r
            out = out.decode()
            err = err.decode()
            rr.append((rtn, out, err))
    results = rr

    results = list(zip(app.cnc_nodes, results))
    return jsonify(results)

@app.route('/cnc-poll/<name>/<pid>')
def cnc_poll(name=None, pid=None):
    client = get_cnc_client()
    res = client.poll(int(pid), 'tcp://' + name)
    return jsonify(res)

def sort_l0_nodes(senders):
    # Assume that senders are IP:port addresses; drop port
    sender_ips = [s.split(':')[0] for s in senders]
    sender_ports = [s.split(':')[1] for s in senders]
    
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
    # If the sender names (just hostnames) are not unique, add the
    # port numbers back in.
    if len(set(sender_names)) != len(I):
        sender_names = [dnsnames[i]+':'+sender_ports[i] for i in I]

    return senders,sender_names

def get_packet_matrix(group_l0=None):
    # Send RPC requests to all nodes, gather results into an HTML table
    client = get_rpc_client()
    # Make RPC requests async.  Timeouts are in *milliseconds*.
    timeout = 5000.

    rates = client.get_packet_rate(timeout=timeout)
    # 'rates' is a list with rpc_client.PacketRate object per L1 node (or None);
    # PacketRate has attributes .start, .period, and .packets;
    # .packets is a sender->npackets mapping.

    if group_l0 == 'node':
        sender_map = {}
        for p in rates:
            if p is None:
                continue
            senders = p.packets.keys()
            for s in senders:
                # ip:port
                ip,port = s.split(':')
                # a.b.c.d
                abcd = [int(x) for x in ip.split('.')]
                # L0 nodes have aliases 10.[6789].x.y == 10.1.x.y
                if abcd[1] in [6,7,8,9]:
                    abcd[1] = 1
                ip = '.'.join(str(x) for x in abcd)
                sender_map[s] = ip + ':' + port
        #print('Applying sender map:', sender_map)
        for i,p in enumerate(rates):
            if p is None:
                continue
            mapped = dict()
            for k,v in p.packets.items():
                k = sender_map[k]
                if not k in mapped:
                    mapped[k] = v
                else:
                    mapped[k] += v
            p.packets = mapped

    senders = set()
    packetrates = []
    for p in rates:
        if p is None:
            packetrates.append({})
            continue
        senders.update(p.packets.keys())
        # Packet counts -> rates
        packetrates.append(dict([(k, v/p.period if p.period > 0 else 0)
                                 for k,v in p.packets.items()]))
        
    senders = list(senders)

    # Parse and sort
    senders,sender_names = sort_l0_nodes(senders)
    return senders, sender_names, packetrates
    
@app.route('/packet-matrix-d3')
def packet_matrix_d3():
    nl0 = request.args.get('nl0', 256)
    return render_template('packets-d3.html',
                           nodes = app.nodes,
                           enodes = enumerate(app.nodes),
                           nl1 = len(app.nodes),
                           nl0 = nl0,
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

def _get_rpc_servers(client, names):
    rservers = dict([(v,k) for k,v in client.servers.items()])
    return [rservers['tcp://' + str(name)] for name in names]

@app.route('/packet-rate-l1-json/<name>')
def packet_rate_l1_json(name=None):
    assert(name is not None)
    client = get_rpc_client()
    timeout = 5000.

    history = int(request.args.get('history', 60))

    servers = _get_rpc_servers(client, [name])
    graph = client.get_packet_rate_history(start=-history,
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

def mymin(scalar, arr):
    if len(arr) == 0:
        return scalar
    am = min(arr)
    if scalar is None:
        return am
    return min(scalar, am)
def mymax(scalar, arr):
    if len(arr) == 0:
        return scalar
    am = max(arr)
    if scalar is None:
        return am
    return max(scalar, am)

@app.route('/packet-rate-l0-json/<ip>')
def packet_rate_l0_json(ip=None):
    from scipy.interpolate import interp1d
    
    assert(ip is not None)
    client = get_rpc_client()
    timeout = 5000.

    history = int(request.args.get('history', 20))

    #print('RPC request for rates of L0 node', ip)
    graphs = client.get_packet_rate_history(start=-history,
                                            l0nodes=[ip],
                                            timeout=timeout)

    # The rate samples we get from the L1 nodes have different sample times;
    # we interpolate them.
    ntotal = len(graphs)
    graphs = [g for g in graphs if g is not None]
    nreplies = len(graphs)

    if len(graphs):
        tmin = None
        tmax = None
        for t,r in graphs:
            tmin = mymin(tmin, t)
            tmax = mymax(tmax, t)

        #times,rates = graphs[0]
        #rate = rates[0]
        # The times where we want to evaluate the rates
        #tgrid = np.array(times)
        #rate = np.array(rate)

        times = np.arange(np.floor(tmin), np.ceil(tmax)+1)
        rate = np.zeros(len(times))

        for t,r in graphs:
            r = r[0]
            if len(t) == 0:
                continue
            # t,r are vectors
            func = interp1d(t, r, kind='linear',
                            bounds_error=False, fill_value=(r[0], r[-1]),
                            assume_sorted=True)
            rate += func(times)
        rate = list(rate)
        times = list(times)
    else:
        times = []
        rate = []

    return jsonify(dict(times=times, rates=rate,
            #alltimes=[t for t,r in graphs],
            #allrates=[r[0] for t,r in graphs],
            nreplies=nreplies, ntotal=ntotal))

@app.route('/packet-matrix.json')
def packet_matrix_json():
    group_l0 = request.args.get('group_l0', None)

    senders, sender_names, packets = get_packet_matrix(group_l0=group_l0)

    # 'packets' is a list with one element per L1 node; each list
    # contains a dict from L0 name (in "senders") to the packet count.

    #print('Packet matrix:', packets)
    
    # Form into a matrix
    npackets = []
    for p in packets:
        row = []
        for s in senders:
            row.append(p.get(s, 0))
        npackets.append(row)

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
    return render_template('node-status.html',
                           node=name,
                           node_status_json_url='/node-status-json/' + name)
    #return redirect('http://localhost:3000/dashboard/db/node-exporter-single-server?orgId=2&from=now-1h&to=now&var-server=%s' % name)

@app.route('/node-status-json/<name>')
def node_status_json(name=None):
    client = get_rpc_client()
    timeout = 5000.
    servers = _get_rpc_servers(client, [name])
    stats = client.get_statistics(servers=servers, timeout=timeout)
    # [0]: first node; [0][0]: just the whole-node stats
    stats = stats[0][0]
    ss = ''
    for k,v in stats.items():
        ss += '  %s = %s\n' % (k, v)
    print('Got stats:', ss)
    return jsonify(stats)



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
