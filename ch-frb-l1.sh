#! /bin/bash

# conda environment
source activate chime-frb-L1b

# Temporary hack -- find my node number 0-7 from hostname cf0g0-7.
# Will not be required once we can use a single l1 yaml file for all nodes
node=$(hostname | cut -c 5)

#./ch-frb-l1 -tv l1_configs/l1_production_8beam_${node}.yaml

./ch-frb-l1 l1_configs/l1_production_8beam_${node}.yaml \
    ../ch_frb_rfi/json_files/rfi_16k/17-12-02-two-pass-yzoom-v3-noplot.json \
    bonsai_configs/bonsai_production_noups_nbeta1_v2.hdf5 \
    L1b_config_site.yaml


