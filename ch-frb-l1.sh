#! /bin/bash

# Assume LD_LIBRARY_PATH is set up in ~/.bashrc
#export LD_LIBRARY_PATH=/home/l1operator/lib:${LD_LIBRARY_PATH}

# Temporary hack -- find my node number 0-7 from hostname cf0-Dg0-7.
# Temporary hack -- find my rack number 0-d from hostname cf0-dg0-7.
node=$(hostname | cut -c 5)
rack=$(hostname | cut -c 3)
./ch-frb-l1 l1_configs/l1_production_8beam_rack${rack}_node${node}.yaml ../ch_frb_rfi/json_files/rfi_16k/17-12-02-two-pass-yzoom-v3.json bonsai_configs/bonsai_production_noups_nbeta1_v2.hdf5 L1b_config_site.yaml

