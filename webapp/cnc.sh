#! /bin/bash

for ((;;)); do
    echo "Starting cnc.py..."
    python cnc.py $*
    sleep 10
done
