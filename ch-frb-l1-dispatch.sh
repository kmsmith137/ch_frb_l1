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

L1_CONFIG=l1_configs/l1_production_8beam_rack${rack}_node${node}.yaml 
L1B_CONFIG=L1b_config_site.yaml

#if true; then
#if false; then
#if [ $(hostname) == cf1n0 ]; then
#if [ $rack == 1 ]; then
#if [ $rack -le 6 ]; then
if [ $(hostname) == cf1n1 ]; then
    #echo "I am $(hostname) aka rack $rack node $node .  Running DEV2 version"
    #export VERSION=dev2

    # Dustin 2019-06-24 -- beam duplication testing
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV3 version"
    VERSION=dev3

    RFI_CONFIG=18-11-15-low-latency-uniform-v1-noplot.json
    BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5

elif [ $(hostname) == cfcn1 ]; then
    # Dustin 2019-06-24 -- beam duplication testing
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV3 version (special)"
    VERSION=dev3

    L1B_CONFIG=L1b_config_fake.yaml
    RFI_CONFIG=18-11-15-low-latency-uniform-v1-noplot.json
    BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5

elif true; then
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV version"
    VERSION=dev
    RFI_CONFIG=19-03-01-low-latency-uniform-nobadchannel-mask-noplot.json
    BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
else
    echo "I am $(hostname) aka rack $rack node $node .  Running PRODUCTION version"
    VERSION=production
    RFI_CONFIG=18-11-15-low-latency-uniform-v1-noplot.json
    BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
fi

export LD_LIBRARY_PATH=/home/l1operator/${VERSION}/lib:/usr/local/lib
export PYTHONPATH=/home/l1operator/${VERSION}/lib/python2.7/site-packages
export PATH=/usr/local/bin:/usr/bin:/bin:/home/l1operator/${VERSION}/bin

cd /home/l1operator/${VERSION}/ch_frb_l1

./ch-frb-l1 ${L1_CONFIG} ../ch_frb_rfi/json_files/rfi_16k/${RFI_CONFIG} /data/bonsai_configs/${BONSAI_CONFIG} ${L1B_CONFIG}
