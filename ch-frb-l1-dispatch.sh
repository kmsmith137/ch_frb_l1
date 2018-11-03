#! /bin/bash

# Temporary hack -- find my node number 0-7 from hostname cf0-Dg0-7.
# Temporary hack -- find my rack number 0-d from hostname cf0-dg0-7.
node=$(hostname | cut -c 5)
rack=$(hostname | cut -c 3)

#if [ $(hostname) == cf1n0 ]; then
#if [ $rack == 1 ]; then
if [ $(hostname) == XXXXcf1n0 ]; then
    echo "I am $(hostname) aka rack $rack node $node .  Running DEV version"
    # Run DEV version
    export LD_LIBRARY_PATH=/home/l1operator/dev/lib:/usr/local/lib
    export PYTHONPATH=/home/l1operator/dev/lib/python2.7/site-packages
    export PATH=/usr/local/bin:/usr/bin:/bin:/home/l1operator/dev/bin
    #### not dev version -- bitshuffle & LZF ??
    export HDF5_PLUGIN_PATH=/home/l1operator/lib/hdf5_plugins

    cd /home/l1operator/dev/ch_frb_l1
    ./ch-frb-l1 l1_configs/l1_production_8beam_rack${rack}_node${node}.yaml ../ch_frb_rfi/json_files/rfi_16k/17-12-02-two-pass-v5-noplot.json /data/bonsai_configs/bonsai_production_noups_nbeta2_5tree_experiment.hdf5 L1b_config_site.yaml
    #18-02-02-rfi-level1-v1-noplot.json 

    exit
fi

# Assume LD_LIBRARY_PATH is set up in ~/.bashrc
#export LD_LIBRARY_PATH=/home/l1operator/lib:${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=/home/l1operator/lib:/usr/local/lib:$LD_LIBRARY_PATH
export PYTHONPATH=/home/l1operator/lib/python2.7/site-packages:$PYTHONPATH
export HDF5_PLUGIN_PATH=/home/l1operator/lib/hdf5_plugins
export PATH=${PATH}:/home/l1operator/bin
./ch-frb-l1 l1_configs/l1_production_8beam_rack${rack}_node${node}.yaml ../ch_frb_rfi/json_files/rfi_16k/17-12-02-two-pass-v5-noplot.json /data/bonsai_configs/bonsai_production_noups_nbeta2_5tree_experiment.hdf5 L1b_config_site.yaml

#./ch-frb-l1 l1_configs/l1_production_8beam_rack${rack}_node${node}.yaml ../ch_frb_rfi/json_files/rfi_16k/17-12-02-two-pass-v3-noplot.json bonsai_configs/bonsai_production_noups_nbeta1_v2.hdf5 L1b_config_site.yaml

