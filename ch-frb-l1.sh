#! /bin/bash

echo "hello on stdout"
(>&2 echo "hello on stderr")

export LD_LIBRARY_PATH=/home/dstn/lib:${LD_LIBRARY_PATH}

./ch-frb-l1 -tv l1_configs/l1_production_8beam_0.yaml

