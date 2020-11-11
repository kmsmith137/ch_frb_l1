#! /bin/bash

export SB=$1

# compare to ~/ch-frb-l1-dispatch.sh

export VERSION=dev5
export LD_LIBRARY_PATH=/home/l1operator/${VERSION}/lib:/usr/local/lib
export PYTHONPATH=/home/l1operator/${VERSION}/lib/python2.7/site-packages
export PATH=/usr/local/bin:/usr/bin:/bin:/home/l1operator/${VERSION}/bin

cd /home/l1operator/${VERSION}/ch_frb_l1

echo "Starting single-beam: ${SB}"
./ch-frb-l1 -f l1_configs/l1_singlebeam_${SB}.yaml \
    ../ch_frb_rfi/json_files/rfi_16k/19-03-01-low-latency-uniform-nobadchannel-mask-noplot.json \
    /data/bonsai_configs/bonsai_production_noups_nbeta2_v4.hdf5 \
    L1b_config_fake.yaml
