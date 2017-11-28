- v6
  - From Dustin: packet_count history RPC + web viewer

- v5
  - Implement 8-beam L1 server
  - Add example 4, an 8-beam L1 server instance with real RFI removal and the most optimal bonsai settings

- v4
  - This large commit is part of the "2017 Mega Merge" affecting many parts of the CHIMEFRB pipeline.
  - Minor changes here and there, matching API changes in underlying libraries
  - terminus-l1 and hdf5-stream phased out (functionality can be ported into ch-frb-l1 if necessary)
  - Non-placeholder RFI removal is now working!

- v3: oops, forgot to increment verison number for a long time, so this is just an outline!
  - Dustin: lots of work on RPC layer (multiple threads, work queues)
  - Dustin: start web app
  - Overhaul L1 server (previous version was really a placeholder)
  - Lots of changes to file-writing code, now documented in doc/17-08-14-file-writing.pdf
  - Write MANUAL.md

- v2: from Dustin:
  - Backward incompatible: lz4, msgpack, zeromq, and cppzmq are now dependencies
  - L1 server module
  - Full python client
  - Partial C++ client
  - Add ch-frb-test.cpp, which simulates a set of L0 nodes and a set of L1 nodes 
    and has them talk to each other (and has L1 RPC servers).
 
- v1: mostly a placeholder, but contains:
  - "global" instructions for compiling the chimefrb pipeline ([INSTALL.md] (./INSTALL.md))
  - pointers to scattered documentation
  - some toy programs for timing the chimefrb networking code
