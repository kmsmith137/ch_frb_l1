#! /bin/bash

#echo "ch-frb-l1-poststop.sh script.  I am $(whoami)"

# It's basically harmless to run this, so don't check which node we are, etc
sudo /home/l1operator/stop-singlebeam.sh


#if [ $(hostname) == cf5n3 ]; then
#fi
