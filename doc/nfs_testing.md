### nfs testing

Here is the procedure I use for testing the L1 server's write speed to NFS,
using the two-node backend.

First, you should build the L1 server code and all of its dependencies on
one or both compute nodes, following [instructions here](./quick_install_l1_node.md).


### Option 1: standalone test of the server write path


This uses the toy program `time-assembled-chunk-write` in the ch_frb_io github repo,
This program is a quick hack to go through the L1 server's write path without actually
running the server.  It could use improvement!  Its initialization time is
artificially large, and it can't write more data than it can precompute in memory.

- From a compute node, create four empty directories on the NFS server, e.g.
  ```
  mkdir /frb-archiver-1/kmsmith/dir1
  mkdir /frb-archiver-2/kmsmith/dir2
  mkdir /frb-archiver-3/kmsmith/dir3
  mkdir /frb-archiver-4/kmsmith/dir4
  ```

- Now open four shells in the ch_frb_io directory, and run the command
  ```
  ./time-assembled-chunk-write -uh /frb-archiver-1/kmsmith/dir1 4   # in shell 1
  ./time-assembled-chunk-write -uh /frb-archiver-2/kmsmith/dir2 4   # in shell 2
  ./time-assembled-chunk-write -uh /frb-archiver-3/kmsmith/dir3 4   # in shell 3
  ./time-assembled-chunk-write -uh /frb-archiver-4/kmsmith/dir4 4   # in shell 4
  ```
  Each of these shells will spend about a minute initializing, then prompt for
  input with `Press return to continue, human!`.

  Notes: 

    - The -u flag writes data uncompressed (currently necessary since our
      compression code is too slow).
    - The -h flag will prompt for human input after initializing, but before writing data.
    - Each shell uses a different /frb-archiver-N directory.  This is a way of
      load-balancing output across the four 1 Gbps NIC's in the node.
   -  The "4" at the end of the command line means that each thread will write 4 GB
      of data, feel free to change.

- Press return in all four shells (as simultaneously as possible).
  Each shell will write files and report its throughput in Gbps.
  The theoretical max is 1 Gbps, and the "gold standard" is 0.55 Gbps (for each
  of the four NIC's).  Right now we are getting a lot less!


### Option 2: running a real server instance.


Coming soon!
