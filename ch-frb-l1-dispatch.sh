#! /bin/bash

# This is the script that launches the L1 process on site.
#
# In production, it is run via systemd, which reads the file
#   /etc/systemd/system/ch-frb-l1.service
# where it becomes the root user, cd's into ~l1operator and runs this script.
# This happens if you click the L1 webapp "Start" button, or if you
# run "service ch-frb-l1 start".
#
# In order to test development versions of our code, we have multiple
# installs of the code: "production" and "dev*".
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

# Defaults:
L1_ARGS=-v
L1_CONFIG=l1_configs/l1_production_8beam.yaml
L1B_CONFIG=L1b_config_site.yaml
#RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
#BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5

if [ $(hostname) == cf1n1 ]; then
    VERSION=dev7
    # Pick up our locally-installed zeromq library...
    export LD_LIBRARY_PATH=/usr/local/zeromq-4.3.4/lib:${LD_LIBRARY_PATH}
elif [ $(hostname) == cf4n2 ]; then
    VERSION=dev7
    # Pick up our locally-installed zeromq library...
    export LD_LIBRARY_PATH=/usr/local/zeromq-4.3.4/lib:${LD_LIBRARY_PATH}

# elif [ $(hostname) == cf5n3 ]; then
#     # singlebeam -- start services
#     sudo /home/l1operator/start-singlebeam.sh
#     sleep 1e6d
#     exit 0

# elif [[ $(hostname) == cf1n7 ]]; then
#     # Davor 2021-04-06: save l1b triggers for SPS fork testing
#     echo "I am $(hostname) aka duplicating beams to SPS dev node and saving triggers.  Running DEV6 version (sender)"
#     VERSION=dev6
#     L1B_CONFIG=L1b_config_save_triggers.yaml

elif [[ $(hostname) == cf1n7 || $(hostname) == cf4n9 ||  $(hostname) == cf8n1 || $(hostname) == cfbn3 || $(hostname) == cf7n3 ]]; then
    echo "I am $(hostname) aka rack $rack node $node.  Running SPS version (saving triggers)"
    VERSION=sps-dstn
    # Needed? L1_ARGS=-b -i
    L1B_CONFIG=L1b_config_save_triggers.yaml
    RFI_CONFIG=21-07-06-low-latency-uniform-badchannel-mask-noplot-spsfirst.json

# Kathryn SPS project
elif [[ $(hostname) == cf2n5 || $(hostname) == cf5n7 ||  $(hostname) == cf8n9 || $(hostname) == cfcn1 || $(hostname) == cf2n6 || $(hostname) == cf5n8 || $(hostname) == cf9n0 || $(hostname) == cfcn2 ]]; then
    echo "I am $(hostname) aka rack $rack node $node.  Running SPS version (saving triggers)"
    VERSION=sps-dstn
    # Needed? L1_ARGS=-b -i
    L1B_CONFIG=L1b_config_save_triggers.yaml
    RFI_CONFIG=21-07-06-low-latency-uniform-badchannel-mask-noplot-spsfirst.json

# Marcus 2020-03-18: changed from cf5n2 to cfan6
elif [ $(hostname) == cfan6 ]; then
     echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (receiver)"
     VERSION=dev6
     L1_ARGS=-f -b -i
     L1_CONFIG=l1_configs/l1_production_8beam_receiver.yaml

# Marcus 2020-06-17: Testing injections on all of rack 5
elif [ $rack == 5 ]; then
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (sender)"
    VERSION=dev6
    L1_ARGS=-b -i

# Alex Roman 2021-08-31 debug node
elif [ $(hostname) == cf7n3 ]; then
    echo "I am $(hostname) aka rack $rack node $node.  Running SPS version (saving triggers)"
    VERSION=apr_debug
    # Needed? L1_ARGS=-b -i
    L1B_CONFIG=L1b_config_save_triggers.yaml
    RFI_CONFIG=21-07-06-low-latency-uniform-badchannel-mask-noplot-spsfirst.json    
# Marcus 2020-04-30: added to test injection fluence against crab pulses
# elif [ $(hostname) == cf4n9 ]; then
#     VERSION=dev6
#     RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
#     #RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
#     #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
#     BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5
# Marcus 2020-06-04: added to test injections in CygA beam 1105
# elif [ $(hostname) == cf5n5 ]; then
#     VERSION=dev6
#     RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
#     #RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
#     #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
#     BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5

else
    # Davor 2020-10-07: Use version dev6 to handle the L0 format with frame0 field
    # Shriharsh 2020-06-24: Started using dev5 version in all nodes.
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version"
    VERSION=dev6
fi

export LD_LIBRARY_PATH="/home/l1operator/${VERSION}/lib:$LD_LIBRARY_PATH"
export PATH="/home/l1operator/${VERSION}/bin:$PATH"
export PYTHONPATH="/home/l1operator/${VERSION}/lib/python2.7/site-packages:/home/l1operator/.local/lib/python2.7/site-packages/:/home/l1operator/.local/lib/python2.7/site-packages/scikit_learn-0.19.1-py2.7-linux-x86_64.egg:$PYTHONPATH"

# To make ch-frb-L1b.py print immediately.
export PYTHONUNBUFFERED=1

cd /home/l1operator/${VERSION}/ch_frb_l1
echo "Running: cd /home/l1operator/${VERSION}/ch_frb_l1 && ./ch-frb-l1 ${L1_ARGS} ${L1_CONFIG} ../ch_frb_rfi/json_files/rfi_16k/${RFI_CONFIG} /data/bonsai_configs/${BONSAI_CONFIG} ${L1B_CONFIG}"
./ch-frb-l1 ${L1_ARGS} ${L1_CONFIG} ../ch_frb_rfi/json_files/rfi_16k/${RFI_CONFIG} /data/bonsai_configs/${BONSAI_CONFIG} ${L1B_CONFIG}
