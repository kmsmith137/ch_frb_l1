Local superhero Dustin Lang is now in charge of this repo!  

Here is everything he needs to know to take over.

### QUICK START

On both compute nodes (frb-compute-0 and frb-compute-1) do the following.
Note that some of these git repositories need to be on non-master branches!
```
git clone https://github.com/kmsmith137/simd_helpers
cd simd_helpers
ln -s site/Makefile.local.norootprivs Makefile.local
make -j20 install
cd ..

git clone https://github.com/kmsmith137/sp_hdf5
cd sp_hdf5
ln -s site/Makefile.local.linux Makefile.local
make -j20 install
cd ..

git clone https://github.com/CHIMEFRB/ch_frb_io
cd ch_frb_io
git checkout v5_devel
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 install
cd ..

git clone https://github.com/CHIMEFRB/bonsai
cd bonsai
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 install
cd ..

git clone https://github.com/kmsmith137/rf_pipelines
cd rf_pipelines
git checkout v15_devel
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 install
cd ..

git clone https://github.com/kmsmith137/ch_frb_l1
cd ch_frb_l1
git checkout v3_devel
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 all
cd ..

# This one is not necessary but it may be useful
git clone https://github.com/kmsmith137/toy_network_codes
cd toy_network_codes
make install
cd ..
```
The example config files assume that frb-compute-0 is the packet sender
(simulated L0 node) and frb-compute-1 is the receiver (L1 node).

First start the L1 server on **frb-compute-1**.
This server instance will dedisperse 16 beams, assuming no link bonding, and
using a relatively tame bonsai config (the dedispersion cores will be ~50% utilized).
```
# on frb-compute-1
cd ch_frb_l1
./new-ch-frb-l1 l1_configs/l1_nobond.yaml bonsai_configs/benchmarks/params_noups_nbeta1.txt
```
After a few seconds, you should see 
```
ch_frb_io: listening for packets, ip_addr=10.0.0.201, udp_port=10252
ch_frb_io: listening for packets, ip_addr=10.0.1.202, udp_port=10252
ch_frb_io: listening for packets, ip_addr=10.0.2.203, udp_port=10252
ch_frb_io: listening for packets, ip_addr=10.0.3.204, udp_port=10252
```
The L1 server is ready to receive packets!  Now go to **frb-compute-0** and start the L0 simulator.
The last command-line argument is the number of seconds of data to simulate.
```
# on frb-compute-0
cd ch_frb_l1
./ch-frb-simulate-l0 l0_configs/l0_nobond.yaml 60
```
No output is written while the client and server are running, but if you run `htop` on the frb-compute-1
you'll see that its cores are busy!

After 60 seconds, you should see a dump of summary information on both nodes.

### MORE INFO

  - Right now there is basically no documentation!  But, I think if you read the simulation code 
    `ch-frb-simulate-l0.cpp` (which has been revamped relative to the master branch), and the L1 
    server `new-ch-frb-l1.cpp` then I bet things will be pretty clear.

  - The directory `ch_frb_l1/bonsai_configs/benchmarks` contains four bonsai config files.  The 
    computational cost of each can be measured with the command-line utility:
    ```
    bonsai-time-dedisperser bonsai_configs/benchmarks/params_noups_nbeta1.txt 20
    ```
    This will run 20 copies of the dedisperser, one on each core, when it measures the running time.
    In addition, the placeholder RFI removal in `new-ch-frb-l1.cpp` should take around 19% of a core.
    Using this procedure, I get the following timings:
    ```
    params_noups_nbeta1.txt: 0.38 + 0.19 = 0.57
    params_noups_nbeta2.txt: 0.42 + 0.19 = 0.61
    params_ups_nbeta1.txt: 0.57 + 0.19 = 0.76
    params_ups_nbeta2.txt: 0.74 + 0.19 = 0.93
    ```
    A puzzle here is that the last config (params_ups_nbeta2.txt) fails to run quickly enough,
    even though it should be able to keep up with the data by a small (7%) margin.

    I usually test using `params_ups_nbeta1.txt`.  This is the most computationally
    expensive option which actually works!

  - Another puzzle is that the UDP packet loss rate is around 10%.  This number was determined
    by comparing the output packet counts (which `ch-frb-simulate-l0` writes to stdout) with
    the input packet counts (which `new-ch-frb-l1` writes to stdout via the `count_packets_good` 
    statistic).

    What makes this strange is that if I try a different test, namely maxing out every core with
    a bonsai dedisperser, running some toy UDP servers (with no core pinning), and sending UDP packets
    from a toy client at the same data rate, then the packet loss rate is much smaller.  (When I
    originally tried this, I got ~2% loss.  I just retried the test and got no packet loss at all!)

    If you would like to reproduce this toy test, here are the steps.  First leave the following
    processes running on the server (frb-compute-1) in separate windows.
    ```
    bonsai-time-dedisperser bonsai_configs/benchmarks/params_ups_nbeta2.txt 20 1000
    udp_server 10.0.0.201 6677
    udp_server 10.0.1.202 6677
    udp_server 10.0.2.203 6677
    udp_server 10.0.3.204 6677
    ```
    The first command will run a very long (1000 times the usual length used for timing)
    dedispersion instance on all 20 cores.

    Then, in separate windows on the client (frb-compute-0) do the following in rapid succession:
    ```
    udp_client 10.0.0.201 6677 -g 0.55 -n 8400 -t 60
    udp_client 10.0.1.202 6677 -g 0.55 -n 8400 -t 60
    udp_client 10.0.2.203 6677 -g 0.55 -n 8400 -t 60
    udp_client 10.0.3.203 6677 -g 0.55 -n 8400 -t 60
    ```
    The udp_client and udp_server utilities are in my toy_network_codes repo.  If you run them
    with no arguments you'll see the meaning of all the flags.

  - Right now the following ports are open on the firewalls of the compute nodes: 10252/udp, 6677/udp, 6677/tcp.
    If you need to open more, the syntax is:
    ```
    sudo firewall-cmd --zone=public --add-port=10252/udp --permanent
    sudo firewall-cmd --reload
    sudo firewall-cmd --list-all
    ```

  - You may run into a nuisance issue where the L1 process hangs for a long time (uninterruptible with control-C)
    after throwing an exception, because it is trying to write its core dump.  My solution is
    `sudo killall -9 abrt-hook-ccpp` in another window.  Let me know if you find a better one!

  - For an up-to-date but probably incomplete list of major loose ends, see TODO.md

  - I'm sure there are lots of things I'm forgetting, just let me know when it would be useful to chat again!
