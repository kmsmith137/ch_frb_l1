### OPERATIONS_MANUAL.md (ch_frb_l1)

The OPERATIONS_MANUAL.md contains instructions for running the L1 server on-site,
in a "cookbook" format with production-scale examples.

See [MANUAL.md](./MANUAL.md) for a systematic tutorial/reference with self-contained examples
and installation instructions.

### CONTENTS

  - [Starting the L1 server](#user-content-starting-the-l1-server)
  - [Cookbook of miscellaneous tasks](#user-content-cookbook-of-miscellaneous-tasks)

<a name="starting-the-l1-server"></a>
### Starting the L1 server

- Right now, you have to start an ssh session by hand on every node, and you also need to use a different
  L1 yaml file on every node!  This will be improved soon.  If you want to start a long-running
  server instance, the `screen` (or `tmux`) utility is useful.

- I suggest that everyone use their own user account for running the L1 server.  This is convenient
  during debugging/commissioning, where we all want to be on different git branches.  When we transition 
  into an "operations" mode, we can switch to using a dedicated account for the L1 server.

- The examples below run a "toy" version of the L1B code which just prints messages as it receives
  coarse-grained triggers.  (It it also supposed to make a trigger plot, but this doesn't work in
  production yet, since the production L1 server has no way of shutting down gracefully.)

- **Example 1**: 16 beams, with "placeholder" RFI removal (detrenders but no clippers), and dedispersion
  using the least optimal settings (no spectral index search, no low-DM upsampled tree).  This won't work
  on real data, since we don't remove RFI!
  ```
  # This starts the L1 server on node 0.  For e.g. node 1, replace cf0g0 by cf0g1, and 
  # replace l1_production_16beam_0.yaml by l1_production_16beam_1.yaml.
  ssh cf0g0
  cd git/ch_frb_l1
  ./ch-frb-l1 l1_configs/l1_production_16beam_0.yaml rfi_configs/rfi_placeholder.json /data/bonsai_configs/bonsai_production_noups_nbeta1_v2.hdf5 l1b_placeholder
  ```

- **Example 2**: 8 beams, with real RFI removal (provisional), and dedispersion with the most optimal settings
  (spectral index search, low-DM upsampled tree).
  ```
  # This starts the L1 server on node 0.  For e.g. node 1, replace cf0g0 by cf0g1, and 
  # replace l1_production_16beam_0.yaml by l1_production_16beam_1.yaml.
  ssh cf0g0
  cd git/ch_frb_l1
  ./ch-frb-l1 l1_configs/l1_production_8beam_0.yaml rfi_configs/rfi_production_v1.json /data/bonsai_configs/bonsai_production_ups_nbeta2_v2.hdf5 l1b_placeholder
  ```

<a name="cookbook-of-miscellaneous-tasks"></a>
### Cookbook of miscellaneous tasks

- Shutting down the L1 server.  Currently the only way to do this is to
  press control-C in the ssh window running the L1 server instance!

- Rebooting an L1 node
  ```
  # ssh to head node
  ssh cf0hn

  # This reboots node 0.  To reboot e.g. node 1, replace cf0i0 by cf0i1.
  ipmitool -I lanplus -H cf0i0 -U ADMIN -P ADMIN power reset
  ```
