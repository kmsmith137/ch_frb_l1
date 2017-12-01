#! /bin/bash

for ((;;)); do
    echo "Starting cnc.py..."
    python webapp/cnc.py $*
    sleep 10
done
