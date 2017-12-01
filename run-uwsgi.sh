#! /bin/bash

export WEBAPP_CONFIG=$1
export FLASK_DEBUG=1

#uwsgi -s /tmp/webapp-dstn.sock --manage-script-name --mount /=webapp:webapp 
uwsgi --socket 0.0.0.0:5120 --processes 8 --plugin python --protocol=http --manage-script-name --mount /=webapp.webapp:app 
#5120

