#! /bin/bash

# Assume LD_LIBRARY_PATH is set up in ~/.bashrc
#export LD_LIBRARY_PATH=/home/l1operator/lib:${LD_LIBRARY_PATH}

source activate chime-frb-L1b

# Temporary hack -- find my node number 0-7 from hostname cf0g0-7.
node=$(hostname | cut -c 5)

#./ch-frb-l1 -tv l1_configs/l1_production_8beam_${node}.yaml

./ch-frb-l1 l1_configs/l1_production_8beam_${node}.yaml ../ch_frb_rfi/json_files/rfi_16k/17-12-02-two-pass-yzoom-v3.json bonsai_configs/bonsai_production_noups_nbeta1_v2.hdf5 L1b_config_site.yaml

