### MANUAL.md

This minimal manual is a work in progress!  It should explain the critical
things you need to know to use the L1 server, but it's pretty terse.

The best way to improve the manual is to get feedback from people who
are seeing it for the first time, so let me know what parts were confusing
or needed more explanation!


### CONTENTS

  - [High-level overview](#user-content-overview)
  - [Quick-start examples which can run on a laptop](#user-content-laptop)
  - [Examples on the two-node McGill backend](#user-content-two-node-backend)
  - [Config file reference: L1 server](#user-content-l1-config)
  - [Config file reference: L0 simulator](#user-content-l0-config)
  - [RPC reference](#user-content-rpc-reference)

<a name="overview"></a>
### HIGH-LEVEL OVERVIEW  

The L1 server is not finished yet!  Here are some caveats:

  - No RFI removal!

  - The simulator only simulates noise; no pulses or RFI.

  - If anything goes wrong it will crash!

  - You will probably encounter bugs.

The main high-level components are:

  - ch-frb-l1: The "L1 server".

  - ch-frb-simulate-l0: The "L0 simulator"

  - RPC client python library.

  - Webapp.

For compilation instructions, see one of the following:
  - doc/install.md: general-purpose install instructions
  - doc/quick_install_frb1.md: simplified install instructions for frb1.physics.mcgill.ca
  - doc/quick_install_l1_node.md: simplified install instructions for L1 nodes (frb-compute-0, frb-compute-1, ...)

<a name="laptop"></a>
### QUICK-START EXAMPLES WHICH CAN RUN ON A LAPTOP


<a name="two-node-backend"></a>
### EXAMPLES ON THE TWO-NODE MCGILL BACKEND

<a name="l1-config"></a>
### CONFIG FILE REFERENCE: L1 SERVER


<a name="l0-config"></a>
### CONFIG FILE REFERENCE: L0 SIMULATOR


<a name="rpc reference"></a>
### RPC REFERENCE

  - get_statistics

  - write_chunks


