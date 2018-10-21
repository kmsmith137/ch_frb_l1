# Note: you probably want to run the webapp through the wrapper 
# script 'run-webapp.sh' in the toplevel ch_frb_l1 directory.

from __future__ import print_function

try:
    basestring
except:
    # py3
    basestring = str

from flask import Flask, render_template, jsonify, request, redirect, url_for, session, flash
import os
import sys
from datetime import datetime, timedelta
import json
import yaml
import msgpack
import numpy as np
import sqlalchemy
from sqlalchemy.orm import sessionmaker, scoped_session
from subprocess import check_output
from functools import wraps
from passlib.hash import sha256_crypt
from chlog_database import LogMessage
from chime_frb_operations import UserAccounts, Acquisition, SignUpForMonitoring, Base

#print('Python version:', sys.version)
# 2.7 on cf0g9

app = Flask(__name__)
app.secret_key="pm30c6DBBOpmA4A4PmJw"

_rpc_client = None
def get_rpc_client():
    global _rpc_client
    if _rpc_client is None:
        from rpc_client import RpcClient
        from collections import OrderedDict
        servers = OrderedDict([(''+str(i), k) for i,k in enumerate(app.nodes)])
        _rpc_client = RpcClient(servers)
    return _rpc_client

def get_cnc_client(user="l1operator"):
    # SSH
    if False:
        #from webapp.cnc_client import CncClient
        from cnc_client import CncClient
        client = CncClient(ctx=app.zmq)
        return client
    from cnc_ssh import CncSsh
    client = CncSsh(ssh_options='-o "User=%s" -o StrictHostKeyChecking=no -i ~/.ssh/id_rsa'%user)
    return client

def get_db_session():
    return app.make_db_session()

def get_acq_db_session():
    return app.make_acq_db_session()

def get_operations_db_session():
    return app.make_operations_db_session()

def get_monitoring_db_session():
    return app.make_monitoring_db_session()

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

    # SSH
    app.cnc_nodes = [n.replace('tcp://', '').replace(':9999','') for n in cnc_nodes]
    app.head_nodes = ['10.5.2.1']

    # FIXME(?): check the format of the node strings here?
    # (Should be something like 'tcp://10.0.0.101:5555')

    database = y.get('log_database', 'sqlite:///log.sqlite3')
    engine = sqlalchemy.create_engine(database)
    app.make_db_session = scoped_session(sessionmaker(bind=engine))

    operations_database = y.get('chime_frb_operations_database', 'sqlite:///chime_frb_operations.sqlite3')
    acq_engine = sqlalchemy.create_engine(operations_database)
    Base.metadata.create_all(acq_engine)
    app.make_acq_db_session = scoped_session(sessionmaker(bind=acq_engine))

    operations_engine = sqlalchemy.create_engine(operations_database)
    Base.metadata.create_all(operations_engine)
    app.make_operations_db_session = scoped_session(sessionmaker(bind=operations_engine))

    monitoring_engine = sqlalchemy.create_engine(operations_database)
    Base.metadata.create_all(monitoring_engine)
    app.make_monitoring_db_session = scoped_session(sessionmaker(bind=monitoring_engine))

parse_config(app)

import zmq
app.zmq = zmq.Context()

@app.route('/')
def home():
    return render_template('home.html')

def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'logged_in' in session:
            return f(*args, **kwargs)
        else:
            flash('Unauthorized, Please login', 'danger')
            return redirect(url_for('login'))
    return decorated_function

@app.route('/calendar', methods=['GET', 'POST'])
@login_required
def calendar():
    if request.method=="POST":
        monitoring_session = get_monitoring_db_session()
        monitoring_info = monitoring_session.query(SignUpForMonitoring).all()
        monitoring_info = [info.as_dict() for info in monitoring_info]
        return jsonify(monitoring_info)
    return render_template('sign_up_for_observations.html')

@app.route('/sign_up_for_monitoring', methods=['POST'])
@login_required
def sign_up_for_monitoring():
    monitoring_session = get_monitoring_db_session()
    args = request.get_json()
    print (args)
    monitoring_id = int(args['monitoring_id'])
    if args['to_delete']:
        obj =  monitoring_session.query(SignUpForMonitoring).get(monitoring_id)
        monitoring_session.delete(obj)
        monitoring_session.commit()
    else:
        name = args['name'] 
        email = args['email'] 
        monitoring_start = args['start'] 
        monitoring_stop = args['stop']
        notes = args['notes']
        to_add = {
                  'name': name,
                  'email': email,
                  'monitoring_start': monitoring_start,
                  'monitoring_stop': monitoring_stop,
                  'notes': notes
                 }

        if monitoring_id:
            monitoring_session.query(SignUpForMonitoring).filter_by(monitoring_id=monitoring_id).update(to_add)
            monitoring_session.commit()
        else:
            add_monitoring_info = SignUpForMonitoring(**to_add)
            monitoring_session.add(add_monitoring_info)
            monitoring_session.commit()
    return jsonify("success")
    
@app.route('/operations')
@login_required
def operations():
    nodes = [n.replace('tcp://','') for n in app.nodes]
    nicenodes = [(n.replace('tcp://','').replace(':5555',''), n.replace('tcp://',''))
                 for n in app.nodes]
    return render_template('operations.html',
                           nodes = nodes,
                           nicenodes = nicenodes,
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

@app.route('/diagnostics')
@login_required
def diagnostics():
    nodes = [n.replace('tcp://','') for n in app.nodes]
    nicenodes = [(n.replace('tcp://','').replace(':5555',''), n.replace('tcp://',''))
                 for n in app.nodes]
    return render_template('diagnostics.html',
                           nodes = nodes,
                           nicenodes = nicenodes,
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

#user login
@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        # Get Form Fields
        username = request.form['username']
        password_candidate = request.form['password']
        operations_session = get_operations_db_session()
        user = operations_session.query(UserAccounts).filter(UserAccounts.username == username).all()
        if len(user):
            data = user[0].as_dict()
            password = data['password']
            if sha256_crypt.verify(password_candidate, password):
                session['logged_in'] = True
                session['username'] = username
                
                flash('You are now logged in', 'success')
                return redirect(url_for('operations'))
            else:
                error = "Invalid Login!"
                return render_template('login.html', error=error)
        else:
            error = "Username not found !"
            return render_template('login.html', error=error)
    return render_template('login.html')

#user logout
@app.route('/logout')
@login_required
def logout():
    session.clear()
    flash('You are now logged out.', 'success')
    return redirect(url_for('home')) 

@app.route('/acq')
@login_required
def acq_page():
    nodes = [n.replace('tcp://','') for n in app.nodes]
    return render_template('acq.html',
                           nodes = list(enumerate(nodes)))

@app.route('/acq-status-json')
@login_required
def acq_status_json():
    client = get_rpc_client()
    # Make RPC requests for list_chunks and get_statistics asynchronously
    timeout = 5000.
    stat = client.stream_status(timeout=timeout)
    print('Got stream status:', stat)
    return jsonify(stat)

@app.route('/acq-start', methods=['POST'])
@login_required
def acq_start():
    client = get_rpc_client()
    #servers_to_exclude = {'0', 'tcp://10.6.201.10:5555'}
    #print (client.servers.keys())
    #del(client.servers['0'])
    #print (client.servers.keys())
    # Make RPC requests for list_chunks and get_statistics asynchronously
    timeout = 5000.
    args = request.get_json()
    acqname = args['acqname']
    acqdev = args['acqdev']
    acqmeta = args['acqmeta']
    acqnew = args.get('acqnew', True);
    acqbeams = args.get('acqbeams', '')
    # print('Beams:', acqbeams)
    if len(acqbeams):
        acqbeams = [int(b) for b in acqbeams.split(',')]
    else:
        acqbeams = []
    print('Sending request to start streaming: name', acqname, 'dev', acqdev,
          'metadata:', acqmeta)
    print('Beams:', acqbeams)
    stat = client.stream(acqname, acq_dev=acqdev, acq_meta=acqmeta,
                         acq_beams=acqbeams, new_stream=acqnew,
                         timeout=timeout)
    #add the excluded server back
    #client.servers['0'] = 'tcp://10.6.201.10:5555'
    #print (client.servers.keys())
    if len(acqbeams): # start acquisition
        acq_session = get_acq_db_session()
        tinfo = check_output('curl carillon:54321/get-frame-time', shell=1)
        frame0_ctime_us = int(1e6*json.loads(tinfo)['frame0_ctime'])
        to_add = {
                  'acquisition_name': acqname,
                  'acquisition_device': acqdev,
                  'acquisition_start': datetime.now(),
                  'beams': str(acqbeams),
                  'frame0_ctime_us': frame0_ctime_us,
                  'notes': acqmeta,
                  'user': session['username']
                 }
        acq = Acquisition(**to_add)
        acq_session.add(acq)
        acq_session.commit()
    print('Start stream status:', stat)
    return jsonify(stat)

@app.route('/acq-history')
@login_required
def acq_history():
    args = request.get_json()
    acq_session = get_acq_db_session()
    acqs = acq_session.query(Acquisition).all()
    columns = [m.key for m in Acquisition.__table__.columns]
    return render_template('acq_history.html',
                           columns=columns,
                           acqs=acqs)



@app.route('/node-service')
@login_required
def node_service():
    # Retrieve systemd/journalctl logs for ch-frb-l1 systemd processes.
    client = get_cnc_client(user="frbadmin")
    #results = client.run('cctl power on cf', app.cnc_nodes,
    #                     timeout=3000)
    #results = client.run('cctl power on cf1n8', app.head_nodes,
    #                     timeout=3000)
    #print ("results", results)
    rr = []
    rack = []
    previous_rack = "1"
    #for node,r in zip(app.cnc_nodes, results):
    for node in app.cnc_nodes:
        try:
            r = check_output(["fping", "-c", "1", "-t100", node])
        except:
            r = None
        current_rack = str(int(node.split('.')[2])-200)
        current_rack = { '10': 'A', '11': 'B', '12': 'C', '13': 'D' }.get(current_rack, current_rack)
        if not (current_rack == previous_rack):
            rr.append({previous_rack: rack})
            rack = []
            previous_rack = current_rack
        if r is None:
            err = '(Node or Network is down)'
            summary = "Off"
            rack.append((node, summary, err))
        else:
            r = (r, "Node is up!", "")
            (rtn, out, err) = r
            print(rtn, out, err)
            out = out.decode('utf-8')
            err = err.decode('utf-8')

            lines = (out + err).split('\n')
            summary = "On"
            rack.append((node, summary, out + err))
    rr.append({previous_rack: rack})
    results = rr
    return render_template('node-service.html',
                           status=results,
                           service_url='/node-service-action')

@app.route('/node-service-action/<action>/<node>')
@login_required
def node_service_action(action=None, node=None):
    assert(action in ['on','off','cycle'])
    if node == 'all':
        perform_action = "cf" 
    elif 'rack' in node:
        perform_action = "cf"+node[4:]
    else:
        perform_action = get_dns_of_node(node)

    client = get_cnc_client(user="frbadmin")
    if (action == 'cycle'):
        cmd = 'cctl power cycle %s' % perform_action
    elif (action == 'off'):
        cmd = 'cctl power off %s' % perform_action
    else:
        cmd = 'cctl power on %s' % perform_action
    rtn = client.run(cmd, app.head_nodes)
    print('Return value:', rtn)
    return redirect('/node-service')

@app.route('/l1-service')
@login_required
def l1_service():
    # Retrieve systemd/journalctl logs for ch-frb-l1 systemd processes.
    client = get_cnc_client()
    results = client.run('sudo -n /bin/systemctl status ch-frb-l1', app.cnc_nodes,
                         timeout=3000)
    print (app.cnc_nodes)
    rr = []
    rack = []
    previous_rack = "1"
    for node,r in zip(app.cnc_nodes, results):
        current_rack = str(int(node.split('.')[2])-200)
        current_rack = { '10': 'A', '11': 'B', '12': 'C', '13': 'D' }.get(current_rack, current_rack)
        if not (current_rack == previous_rack):
            rr.append({previous_rack: rack})
            rack = []
            previous_rack = current_rack
        if r is None:
            err = '(failed to retrieve status)'
            rack.append((node, err, err))
        else:
            (rtn, out, err) = r
            print(rtn, out, err)
            out = out.decode('utf-8')
            err = err.decode('utf-8')

            lines = (out + err).split('\n')
            # Look for "Active:" line.
            summary = '(status not found)'
            for l in lines:
                if 'Active:' in l:
                    summary = l
                    break
            rack.append((node, summary, out + err))
    rr.append({previous_rack: rack})
    results = rr
    return render_template('l1-service.html',
                           status=results,
                           service_url='/l1-service-action')

@app.route('/l1-service-action/<action>/<node>')
@login_required
def l1_service_action(action=None, node=None):
    if node == 'all':
        nodes = app.cnc_nodes
    elif 'rack' in node:
        nodes = get_rack_of_nodes(node)
    else:
        nodes = [node]

    assert(action in ['start','stop'])

    client = get_cnc_client()
    cmd = 'sudo -n /bin/systemctl %s ch-frb-l1' % action
    rtn = client.run(cmd, nodes)
    print('Return value:', rtn)
    return redirect('/l1-service')

def get_rack_of_nodes(rack):
    rack = rack[4:]
    rack = { 'A': '10', 'B': '11', 'C': '12', 'D': '13' }.get(rack, rack)
    rack = int(rack)+200
    return [node for node in app.cnc_nodes if "."+str(rack)+"." in node]

def get_dns_of_node(node):
    node_split = node.split('.')
    rack = str(int(node_split[2])-200)
    rack = { 'A': '10', 'B': '11', 'C': '12', 'D': '13' }.get(rack, rack)
    node_num = str(int(node_split[-1])-10)
    return "cf"+rack+"n"+node_num

@app.route('/l1-logs-stdout')
@login_required
def l1_logs_stdout():
    # Retrieve systemd/journalctl logs for ch-frb-l1 systemd processes.
    client = get_cnc_client()
    results = client.run('/bin/journalctl -u ch-frb-l1 -n 20', app.cnc_nodes,
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
    #print('Results:', results)
    #print('CNC nodes:', app.cnc_nodes)

    logmsgs = []
    for node,rr in zip(app.cnc_nodes, results):
        if rr is None:
            logmsgs.append((node, ['Failed to retrieve logs']))
        else:
            rtn,out,err = rr
            lines = out.split('\n') + err.split('\n')
            lines = [x.strip() for x in lines]
            lines = [x for x in lines if len(x)]
            logmsgs.append((node, lines))
    #print('msgs:', logmsgs)
    return render_template('l1-logs-stdout.html',
                           logmsgs=logmsgs)

@app.route('/l1-logs-recent')
@login_required
def l1_logs_recent():
    log_session = get_db_session()
    datecut = datetime.now() - timedelta(0, 60)
    logs = log_session.query(LogMessage).filter(LogMessage.date >= datecut)
    filters = [('date after ' + str(datecut), 'date_gt_%s' % str(datecut).replace(' ','_'))]
    return render_template('l1-logs-recent.html',
                           logs=logs,
                           logfilters=filters)

@app.route('/l0-node-map')
@login_required
def l0_node_map():
    return render_template('l0-node-map.html',
                           packet_matrix_json_url='/packet-matrix.json',
                           racks=list(enumerate(['%x' % x for x in range(15)])),
                           nodesperrack=[0,1,2,3,4,5,6,7,8,9])

@app.route('/cnc-kill', methods=['POST'])
def cnc_kill():
    #if request.method != 'POST':
    #    return 'POST only'
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
    #print('Got results:')
    rr = []
    for node,r in zip(app.cnc_nodes, results):
        #print('  ', r)
        if r is None:
            rr.append((node, None))
        else:
            (rtn, out, err) = r
            out = out.decode()
            err = err.decode()
            rr.append((node, (rtn, out, err)))
    results = rr
    #print('Results:', results)
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
@login_required
def packet_matrix_d3():
    nl0 = request.args.get('nl0', 256)
    return render_template('packets-d3.html',
                           nodes = app.nodes,
                           enodes = enumerate(app.nodes),
                           nl1 = len(app.nodes),
                           nl0 = nl0,
                           packet_matrix_json_url='/packet-matrix.json',)

@app.route('/packets-l0/<name>/<ip>')
@login_required
def packets_l0(name=None, ip=None):
    return render_template('packets-l0-d3.html',
                           node0name=name,
                           node0ip=ip,
                           nodes1=app.nodes)

@app.route('/packets-l1/<name>')
@login_required
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
@login_required
def packet_matrix():
    senders, sender_names, packets = get_packet_matrix()
   
    #html = '<html><body>'
    html = '<html>'
    html +='<link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/4.0.0/css/bootstrap.min.css" integrity="sha384-Gn5384xqQ1aoWXA+058RXPxPg6fy4IWvTNh0E263XmFcJlSAwiGgFAW/dAiS6JXm" crossorigin="anonymous">'
    html += '<body>'
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
@login_required
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
@login_required
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
@login_required
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
@login_required
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
