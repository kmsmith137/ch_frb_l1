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

L1_ARGS=-v
#L1_CONFIG=l1_configs/l1_production_8beam_rack${rack}_node${node}.yaml 
L1_CONFIG=l1_configs/l1_production_8beam.yaml
L1B_CONFIG=L1b_config_site.yaml

# The commented sections below are from previous testing of various development
# L1 versions. They should probably be ignored for the most part, unless trying
# to understand how to deploy a previous development version for testing!

# -------------------------------------------------------------------------------

# if [ $(hostname) == cf1n1 ]; then
#     # echo "I am $(hostname) aka rack $rack node $node .  Running DEV2 version"
#     # VERSION=dev2
#     # 
#     # #echo "I am $(hostname) aka rack $rack node $node .  Running DEV3 version"
#     # #VERSION=dev3
#     # 
#     # RFI_CONFIG=18-11-15-low-latency-uniform-v1-noplot.json
#     # BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
# 
#     # Dustin 2019-09-06 - testing variance retrieval
#     #VERSION=dev4
#     #RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
#     #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
#     # L0 DEBUGGING
#     VERSION=dev3
#     RFI_CONFIG=18-11-15-low-latency-uniform-v1-noplot.json
#     BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5


# if [ $(hostname) == cf5n1 ]; then
#     # Dustin 2020-01-31 -- dynamic beam id & restart
#     echo "I am $(hostname) aka rack $rack node $node .  Running DEV5 version (beamid)"
#     VERSION=dev5
#     L1_ARGS=-f
#     L1_CONFIG=cf1n1.yaml
#     RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
#     BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5

# if [ $(hostname) == cf5n3 ]; then
# 
#     # singlebeam -- start services
#     sudo /home/l1operator/start-singlebeam.sh
#     sleep 1e6d
#     exit 0
# 
#     # Dustin 2020-02-06 -- dynamic beam id & restart
#     echo "I am $(hostname) aka rack $rack node $node .  Running DEV5 version (beamid, sender)"
#     VERSION=dev5
#     L1_CONFIG=l1_configs/l1_production_8beam.yaml
#     RFI_CONFIG=19-03-01-low-latency-uniform-nobadchannel-mask-noplot.json
#     BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5

# -------------------------------------------------------------------------------

if [ $(hostname) == cf5n0 ]; then
    # Dustin 2019-07-22 -- beam duplication testing
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (sender)"
    VERSION=dev6
    L1_ARGS=-b -i
    RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
    #RFI_CONFIG=19-03-01-low-latency-uniform-nobadchannel-mask-noplot.json
    #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
    BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5

# elif [[ $(hostname) == cf1n7 ]]; then
#     # Davor 2021-04-06: save l1b triggers for SPS fork testing
#     echo "I am $(hostname) aka duplicating beams to SPS dev node and saving triggers.  Running DEV6 version (sender)"
#     VERSION=dev6
#     RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
#     BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5
#     # the only difference from the production config:
#     L1B_CONFIG=L1b_config_save_triggers.yaml

# # Alex Roman 2021-03-21: test sps writer branches
# elif [[ $(hostname) == cf1n7 ||$(hostname) == cf4n9 ||  $(hostname) == cf8n1 || $(hostname) == cfbn3 ]]; then
#     echo "I am $(hostname) aka rack $rack node $node .  Running SPS version (sender)"
#     VERSION=sps
#     L1_ARGS=-b -i
#     RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
#     # RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
#     #RFI_CONFIG=19-03-01-low-latency-uniform-nobadchannel-mask-noplot.json
#     #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
#     BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5


# Marcus 2020-03-18: changed from cf5n2 to cfan6
elif [ $(hostname) == cfan6 ]; then
     echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (receiver)"
     VERSION=dev6
     L1_ARGS=-f -b -i
     L1_CONFIG=l1_configs/l1_production_8beam_receiver.yaml
     RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
     #RFI_CONFIG=19-03-01-low-latency-uniform-nobadchannel-mask-noplot.json
     #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
     BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5

# Marcus 2020-06-17: Testing injections on all of rack 5
elif [ $rack == 5 ]; then
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (sender)"
    VERSION=dev6
    L1_ARGS=-b -i
    RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
    #RFI_CONFIG=19-03-01-low-latency-uniform-nobadchannel-mask-noplot.json
    #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
    BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5

# Marcus 2020-04-30: added to test injection fluence against crab pulses
elif [ $(hostname) == cf4n9 ]; then
    # Dustin 2019-07-22 -- beam duplication testing
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (sender)"
    VERSION=dev6
    RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
    #RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
    #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
    BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5

# Marcus 2020-06-04: added to test injections in CygA beam 1105
elif [ $(hostname) == cf5n5 ]; then
    # Dustin 2019-07-22 -- beam duplication testing
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (sender)"
    VERSION=dev6
    RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
    #RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
    #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
    BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5

## Chitrang 2020-10-13: fixed coarse graining test
elif [ $(hostname) == cf1n1 ]; then
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (sender)"
    VERSION=dev6
    RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
    #RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
    BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5

else
    #    echo "I am $(hostname) aka rack $rack node $node .  Running DEV version"
    #    VERSION=dev
    # Davor 2020-10-07: Use version dev6 to handle the L0 format with frame0 field
    # Shriharsh 2020-06-24: Started using dev5 version in all nodes.
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV6 version (sender)"
    VERSION=dev6
    RFI_CONFIG=21-03-07-low-latency-uniform-badchannel-mask-noplot.json
    #RFI_CONFIG=20-09-10-low-latency-uniform-badchannel-mask-noplot.json
    #BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
    BONSAI_CONFIG=bonsai_production_fixed_coarse_graining_hybrid_0.8_0.015.hdf5
fi

# else
#     echo "I am $(hostname) aka rack $rack node $node .  Running PRODUCTION version"
#     VERSION=production
#     RFI_CONFIG=18-11-15-low-latency-uniform-v1-noplot.json
#     BONSAI_CONFIG=bonsai_production_noups_nbeta2_v4.hdf5
# fi

#export LD_LIBRARY_PATH=/home/l1operator/${VERSION}/lib:/usr/local/lib:$LD_LIBRARY_PATH
#export PYTHONPATH=/home/l1operator/${VERSION}/lib/python2.7/site-packages:$PYTHONPATH
#export PATH=/usr/local/bin:/usr/bin:/bin:/home/l1operator/${VERSION}/bin:$PATH

export LD_LIBRARY_PATH="/home/l1operator/${VERSION}/lib:$LD_LIBRARY_PATH"
export PATH="/home/l1operator/${VERSION}/bin:$PATH"
export PYTHONPATH="/home/l1operator/${VERSION}/lib/python2.7/site-packages:/home/l1operator/.local/lib/python2.7/site-packages/:/home/l1operator/.local/lib/python2.7/site-packages/scikit_learn-0.19.1-py2.7-linux-x86_64.egg:$PYTHONPATH"

echo "cd /home/l1operator/${VERSION}/ch_frb_l1"
echo "./ch-frb-l1 ${L1_ARGS} ${L1_CONFIG} ../ch_frb_rfi/json_files/rfi_16k/${RFI_CONFIG} /data/bonsai_configs/${BONSAI_CONFIG} ${L1B_CONFIG}"
echo "../ch_frb_rfi/json_files/rfi_16k/${RFI_CONFIG}"
echo " /data/bonsai_configs/${BONSAI_CONFIG}"
echo " ${L1B_CONFIG}"

cd /home/l1operator/${VERSION}/ch_frb_l1
echo "Running: ./ch-frb-l1 ${L1_ARGS} ${L1_CONFIG} ../ch_frb_rfi/json_files/rfi_16k/${RFI_CONFIG} /data/bonsai_configs/${BONSAI_CONFIG} ${L1B_CONFIG}"
./ch-frb-l1 ${L1_ARGS} ${L1_CONFIG} ../ch_frb_rfi/json_files/rfi_16k/${RFI_CONFIG} /data/bonsai_configs/${BONSAI_CONFIG} ${L1B_CONFIG}
