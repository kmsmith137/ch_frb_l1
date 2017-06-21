### ch_frb_l1 

Toplevel repository for the CHIMEFRB L1 code, containing master executables, bonsai configs, etc.

The L1 code consists of the following git repositories.
Depending on what you're doing, you may only need a subset of these!
In particular, the modules marked "frb1 only" include hardcoded pathnames on
frb1.physics.mcgill.ca, and probably won't be useful on other machines.
For a dependency graph, see [dependencies.png](./dependencies.png).

  - [kiyo-masui/bitshuffle](https://github.com/kiyo-masui/bitshuffle):
    "bitwise" compression algorithm used throughout CHIME.
  - [kmsmith137/simd_helpers](https://github.com/kmsmith137/simd_helpers):
    header-only library for writing x86 assembly language kernels.
  - [kmsmith137/sp_hdf5](https://github.com/kmsmith137/sp_hdf5):
    header-only library for reading/writing hdf5 from C++.
  - [kmsmith137/simpulse](https://github.com/kmsmith137/simpulse):
    library for simulating FRB's and pulsars.
  - [CHIMEFRB/ch_frb_io](https://github.com/CHIMEFRB/ch_frb_io):
    networking code, CHIME-specific file formats.
  - [CHIMEFRB/bonsai](https://github.com/CHIMEFRB/bonsai):
    fast tree dedispersion on x86.
  - [kmsmith137/rf_pipelines](https://github.com/kmsmith137/rf_pipelines):
    plugin-based radio astronomy pipelines.
  - [mrafieir/ch_frb_rfi](https://github.com/mrafieir/ch_frb_rfi):
    scritping framework for RFI removal and offline L1 analysis.  **(frb1 only)**
  - [mburhanpurkar/web_viewer](https://github.com/mburhanpurkar/web_viewer):
    web-based visualization of L1 pipeline outputs.  **(frb1 only)**
  - [kmsmith137/ch_frb_l1](https://github.com/kmsmith137/ch_frb_l1):
    toplevel repo, whose README you're reading right now.

There are a lot of external dependencies!
For a list, see [doc/install.md](./doc/install.md).

### QUICK-START INSTALLATION ON CHIME MACHINES


If you're using one of the CHIME machines (frb1, frb-compute-X), then all
external dependencies should already be installed, and you can use one of the
following "cheat sheets" to install the L1 pipeline from scratch.

  - [Installing from scratch on frb1.physics.mcgill.ca](./doc/quick_install_frb1.md)
  - [Installing from scratch on an frb-compute-X compute node](./doc/quick_install_l1_node.md)


### HELP! MY PIPELINE IS BROKEN

Since the pipeline is under continuous development, and updates to pipeline
modules depend on each other, at some point you may find yourself with an 
inconsistent set of modules.  In this case, you can use the following
"cheat sheet" to put all pipeline modules on their master branches,
update from git, and rebuild everything from scratch.

   - [Rebuilding the pipeline](./doc/rebuilding_pipeline.md)


### INSTALLATION (NON-QUICK-START VERSION)

If you're not installing on a CHIME machine, then the installation process
is more involved.  You'll probably need to write some Makefile include files
("Makefile.local" files).  We hope to streamline this process at some point!
For now, please see instructions in [doc/install.md] (./doc/install.md).


### THE DOCUMENTATION EASTER EGG HUNT

The bad news: documentation for the L1 pipeline is hit-or-miss, and scattered
all over the place (e.g. in code comments or Python docstrings).

We'll do a sytematic "documentation pass" at some point, but I think it will make
more sense to do this when the code is a bit more converged.

In the meantime, here are some links to get you started on the 
"Documentation Easter Egg Hunt":

  - ch_frb_io
     - There are some PDF slides in [ch_frb_io/docs] (https://github.com/CHIMEFRB/ch_frb_io/tree/master/docs).
     - The official spec for the packet protocol is [L0_L1_packet.hpp] (https://github.com/CHIMEFRB/ch_frb_io/blob/master/L0_L1_packet.hpp)
     - The header file [ch_frb_io.hpp] (https://github.com/CHIMEFRB/ch_frb_io/blob/master/ch_frb_io.hpp)
       should be a pretty readable overview of the C++ API.
     - For toy programs which send and receive network streams, see
       [ch-frb-simulate-l0.cpp] (./ch-frb-simulate-l0.cpp) and [ch-frb-l1.cpp] (./ch-frb-l1.cpp)
       in this github repo.
     - The python interface to the networking code is in rf_pipelines, not ch_frb_io.
       For toy python pipelines which send and receive network scripts, see
       [rf_pipelines/examples/example5_network] (https://github.com/kmsmith137/rf_pipelines/tree/master/examples/example5_network).

  - rf_pipelines

     - Warning: this core library is under-documented, and improving this is our highest documentation priority!
     - There are some PDF slides in [rf_pipelines/docs] (https://github.com/kmsmith137/rf_pipelines/tree/master/docs).
     - The rf_pipelines package is best "documented by example", so I recommend looking at
       [rf_pipelines/examples] (https://github.com/kmsmith137/rf_pipelines/tree/master/examples) next.
     - The python docstrings should collectively be a pretty complete form of documentation for the rf_pipelines python API.
       (Is there a convenient way to browse python docstrings?  I just use the python interpreter, which is pretty klunky.)
       Some docstring "greatest hits":
    ```
        import rf_pipelines
        help(rf_pipelines)                   # overview
        help(rf_pipelines.py_wi_transform)   # base class from which all python transforms are derived
        help(rf_pipelines.py_wi_stream)      # base class from which all python streams are derived
        dir(rf_pipelines)                    # lists contents of rf_pipelines module

        # for each entry of 'dir(rf_pipelines)', a docstring is available
        help(rf_pipelines.chime_stream_from_acqdir)   
    ```
     - The header file [rf_pipelines.hpp] (https://github.com/kmsmith137/rf_pipelines/blob/master/rf_pipelines.hpp)
       should be a pretty readable overview of the rf_pipelines C++ api.

  - bonsai

     - The bonsai package now has lots of documentation!  Just start with 
       [bonsai/README.md](https://github.com/CHIMEFRB/bonsai/blob/master/README.md)
       and follow links from there.

  - ch_frb_rfi

     - Not much documentation yet, but the example scripts
       [ch_frb_rfi/example.py](https://github.com/mrafieir/ch_frb_rfi/blob/master/example.py) and
       [ch_frb_rfi/example2.py](https://github.com/mrafieir/ch_frb_rfi/blob/master/example2.py)
       should give a pretty good idea of how this library is used.

