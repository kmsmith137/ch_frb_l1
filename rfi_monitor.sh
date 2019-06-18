#! /bin/bash

export PATH=/data/frb-archiver/dstn/miniconda3/bin:$PATH
source activate /data/frb-archiver/dstn/conda-env
cd /data/frb-archiver/dstn/rfi_monitor
python -u rfi_recorder.py   # > rfi.log 2>&1

