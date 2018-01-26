from __future__ import print_function
import threading
from datetime import datetime
import zmq
import sqlalchemy
from rpc_client import RpcClient

from sqlalchemy.orm import sessionmaker
from sqlalchemy.orm import scoped_session

from chlog_database import LogMessage
    
class ChLogServer(threading.Thread):
    def __init__(self, addr='tcp://127.0.0.1:*', context=None,
                 daemon=True):
        super(ChLogServer, self).__init__()
        self.daemon = daemon
        if context is None:
            self.context = zmq.Context()
        else:
            self.context = context
        self.socket = self.context.socket(zmq.SUB)
        self.socket.subscribe('')
        
        self.socket.bind(addr)
        addr = self.socket.get(zmq.LAST_ENDPOINT)
        print('Bound to', addr)
        self.address = addr
        self.start()

    def run_start(self):
        pass
        
    def run(self):
        self.run_start()
        while True:
            parts = self.socket.recv_multipart()
            self.handle_message(parts)
            
class ChLogToDatabase(ChLogServer):
    def __init__(self, database=None, **kwargs):
        super(ChLogToDatabase, self).__init__(**kwargs)

        import re
        self.regex = re.compile(r'^(?P<host>\S*) ' +
                                r'(?P<thread>\S*) ' +
                                r'(?P<level>\w*) ' +
                                # r'(?P<fileline>\S*) ' + 
                                r'(?P<filename>\S*):(?P<linenum>\d*) ' +
                                r'\[(?P<function>.*)\] ' +
                                r'(?P<date>\S*) ' +
                                r'(?P<msg>.*)')

        self.engine = sqlalchemy.create_engine(database, echo=True)
        from chlog_database import Base
        Base.metadata.create_all(self.engine)
        self.session_maker = scoped_session(sessionmaker(bind=self.engine))
        
    def handle_message(self, parts):
        #print('Got', parts)
        assert(len(parts) == 1)
        msg = parts[0]
        print('Got:', msg)
        match = self.regex.match(msg)

        parts = dict([(k, match.group(k)) for k in
                      ['host', 'thread', 'level', #'fileline',
                       'filename', 'linenum',
                       'function', 'date', 'msg']])
        print('Parsed:', parts)
        # 2018-01-09-20:44:27.827
        parts['date'] = datetime.strptime(parts['date'], '%Y-%m-%d-%H:%M:%S.%f')

        print('Has table?', self.engine.has_table('log'))

        session = self.session_maker()
        log = LogMessage(**parts)
        session.add(log)
        session.commit()
        
def main():
    # Expect to be run with a YAML config file as argument.
    # (eg, l1_configs/l1_production_8beam_webapp.yaml)
    import sys
    import yaml
    
    if len(sys.argv) != 2:
        print('Use: %s <config.yaml>' % sys.argv[0])
        return -1
    config_fn = sys.argv[1]
    try:
        y = yaml.load(open(config_fn))
    except:
        print("ch-logger: couldn't parse yaml config file '%s'" % config_fn)
        import traceback
        traceback.print_exc()
        return -1

    if not isinstance(y,dict) or not 'rpc_address' in y:
        print("ch-logger: no 'rpc_address' field found in yaml file '%s'" % config_fn)
        return -1

    nodes = y['rpc_address']
    # allow a single string (not a list)
    if isinstance(nodes, basestring):
        nodes = [nodes]
    if not isinstance(nodes,list) or not all(isinstance(x, basestring) for x in nodes):
        print("ch_logger: expected 'rpc_address' field to be a list of strings in config file %s" % config_fn)
        return -1

    database = y.get('log_database', 'sqlite:///log.sqlite3')
    print('Using log database:', database)
    
    servers = dict([(i,k) for i,k in enumerate(nodes)])
    client = RpcClient(servers, identity='ch-logger')
    
    logaddr = y.get('logger_address', None)
    if logaddr is None:
        logaddr = 'tcp://eno1:*'

    # Parse IP address part of name
    hostname = logaddr.replace('tcp://', '')
    hostname,port = hostname.split(':')
    try:
        import socket
        ip = socket.gethostbyname(hostname)
    except:
        # Is it a network interface name?
        import netifaces
        try:
            ip = netifaces.ifaddresses(hostname)[netifaces.AF_INET][0]['addr']
            print('Converting network interface name', hostname, 'to', ip)
            hostname = ip
        except:
            # Not a network interface name.  Continue but expect to fail...?
            pass
    logaddr = 'tcp://' + hostname + ':' + port
    print('Logging to address:', logaddr)
    
    logger = ChLogToDatabase(addr=logaddr, daemon=False,
                             database=database)
    addr = logger.address
    print('Telling L1 processes to log to', addr)

    logger.handle_message(['slinky.local L1-RPC-server INFO l1-rpc.cpp:412 [int L1RpcServer::_handle_request(zmq::message_t *, zmq::message_t *)] 2018-01-09-20:44:27.827 chlog remote logging request received.  Hello!'])

    client.start_logging(addr)


    engine = sqlalchemy.create_engine(database)
    session_maker = sessionmaker(bind=engine)
    session = session_maker()
    
    while True:
        import time
        time.sleep(3.)

        # Send RPCs
        client.get_statistics()

        time.sleep(3.)
        
        # Query DB.
        #engine = logger.engine #sqlalchemy.create_engine(database, echo=True)
        #session_maker = sessionmaker(bind=engine)
        #session = session_maker()
        #session = logger.session_maker()
        print('Log messages:')
        for logmsg in session.query(LogMessage).order_by(LogMessage.date):
            print('  ', logmsg)
    
    #logger.join()
    
if __name__ == '__main__':
    import sys
    sys.exit(main())
    
