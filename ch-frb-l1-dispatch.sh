#! /bin/bash

# This is the script that launches the L1 process on site.
#
# In production, it is run via systemd, which reads the file
#   /etc/systemd/system/ch-frb-l1.service
# where it becomes the l1operator user, cd's into ~ and runs this script.
# This happens if you click the L1 webapp "Start" button, or if you
# run "service ch-frb-l1 start".
#
# In order to test development versions of our code, we have two
# parallel installs of the code: "production" and "dev".
#
# Normally, we test a dev version by first running on one node
# (usually cf1n0), then running on a full rack.  See the "if"
# statement below to comment-in-or-out where you want to deploy.
#
# When re-building the "production" or "dev" version, please use the
# "build.sh" script, or set the environment variables that are set at
# the top of that script, or order to install in the correct
# directories.
#

# Temporary hack -- find my node number 0-7 from hostname cf0-Dg0-7.
# Temporary hack -- find my rack number 0-d from hostname cf0-dg0-7.
node=$(hostname | cut -c 5)
rack=$(hostname | cut -c 3)

#if [ $rack == 1 ]; then
#if [ $(hostname) == cf1n0 ]; then
#if true; then
if false; then
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV version"
    export VERSION=dev
    export RFI_CONFIG=18-11-15-low-latency-uniform-v1-noplot.json
    export BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
else
    echo "I am $(hostname) aka rack $rack node $node .  Running PRODUCTION version"
    export VERSION=production
    export RFI_CONFIG=18-11-15-low-latency-uniform-v1-noplot.json
    export BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
fi

export LD_LIBRARY_PATH=/home/l1operator/${VERSION}/lib:/usr/local/lib
export PYTHONPATH=/home/l1operator/${VERSION}/lib/python2.7/site-packages
export PATH=/usr/local/bin:/usr/bin:/bin:/home/l1operator/${VERSION}/bin

cd /home/l1operator/${VERSION}/ch_frb_l1

# Environment variable "prometheus_multiproc_dir" is required for the
# Prometheus Python client to operate in the multi-process mode. (See
# https://github.com/prometheus/client_python#multiprocess-mode-gunicorn)
# To avoid metric pollution across execution, we use a unique directory
# per run, and remove it afterwards.
export prometheus_multiproc_dir=$(mktemp --dir --tmp "l1b_metrics.XXXX")

# Run a separate Python process just for reporting shared metrics
cat <<EOF | python & <<EOF
import prometheus_client as prom
import prometheus_client.multiprocess as mp
import time

registry = prom.CollectorRegistry()
mp.MultiProcessCollector(registry)
prom.start_http_server(8080, registry=registry)

while True:
  time.sleep(10)
EOF

./ch-frb-l1 l1_configs/l1_production_8beam_rack${rack}_node${node}.yaml ../ch_frb_rfi/json_files/rfi_16k/${RFI_CONFIG} /data/bonsai_configs/${BONSAI_CONFIG} L1b_config_site.yaml

# cleanup Prometheus multiproc setup
if [ -d "${prometheus_multiproc_dir}" ]; then
    rm "${prometheus_multiproc_dir}"/*.db
    rmdir "${prometheus_multiproc_dir}"
fi
