#!/bin/bash

export PATH="/home/l1operator/anaconda2/bin:$PATH"

if [[ $# -ne 1 ]]; then
    echo "usage: run-webapp.sh <l1_config.yaml>"
    echo
    echo "Currently, the L1 config file only needs to contain the key 'rpc_address', which should"
    echo "be a list of RPC server locations in the format 'tcp://10.0.0.101:5555'.  Note"
    echo "that an l1_config file will probably work, since it contains the 'rpc_address'"
    echo "key among others."
    echo
    echo "The webapp will run on port 5120 -- see run-uwsgi.sh for the thing running on port 5002!"

    exit 1
fi

export FLASK_APP=webapp.webapp 
export WEBAPP_CONFIG=$1

# Note: FLASK_DEBUG disabled here, since it would allow execution of 
# arbitrary code over the internet!  (--host 0.0.0.0)
#flask run --host 0.0.0.0 --port 5002

# We're behind firewalls, so live dangerously
export FLASK_DEBUG=1

export PYTHONPATH=${PYTHONPATH}:.

flask run --host 0.0.0.0 --port 5120 #--with-threads
