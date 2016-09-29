ch_frb_l1: this will eventually be the toplevel repository for the CHIMEFRB L1 node,
containing master executables, bonsai configs, etc.

Right now it's a placeholder which just contains:
  - "global" instructions for compiling the chimefrb pipeline ([INSTALL.md] (./INSTALL.md))
  - pointers to scattered documentation
  - some toy programs for timing the chimefrb networking code

### INSTALLATION

The CHIMEFRB pipeline currently consists of the following external dependencies:

  - A gcc which is recent enough that C++11 and AVX2 are supported.  I know that gcc 4.8.1 works, and that 4.4.7 is too old.
  - A very recent cython.  I know that cython 0.24 works, and cython 0.20 is too old.
  - numpy/scipy/matplotlib
  - hdf5 version 1.8.11 or later
  - h5py version 2.4.0 or later
  - FFTW3 (http://fftw.org)
  - The 'PIL' python imaging library.  If you need to install it, I recommend the Pillow variant ('pip install Pillow')
  - jsoncpp

plus the following components:

  - bitshuffle (https://github.com/kiyo-masui/bitshuffle)
  - simpulse (https://github.com/kmsmith137/simpulse)
  - ch_frb_io (https://github.com/CHIMEFRB/ch_frb_io)
  - bonsai (https://github.com/CHIMEFRB/bonsai)
  - rf_pipelines (https://github.com/kmsmith137/rf_pipelines)
  - ch_frb_l1, whose README.md you're reading right now (https://github.com/kmsmith137/ch_frb_l1)

For detailed instructions on how to build these packages, see [INSTALL.md] (./INSTALL.md).


### THE DOCUMENTATION EASTER EGG HUNT

Documentation for the CHIMEFRB pipeline is currently not very systematic!
The total documentation available is not bad, but it's scattered all over the
place (e.g. in code comments or Python docstrings).

Here are some links to get you started on the "Documentation Easter Egg Hunt":

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
     - The bonsai package is not as well documented as the others, I hope to improve this soon!
     - Some PDF slides: [bonsai/kms_16_01_13.pdf] (https://github.com/CHIMEFRB/bonsai/blob/master/kms_16_01_13.pdf),
       [bonsai/kms_16_05_03.pdf] (https://github.com/CHIMEFRB/bonsai/blob/master/kms_16_05_03.pdf).
     - At a practical level, the most important thing to understand for purposes of running the bonsai code
       is the format and meaning of its config file.  Right now the best documentation is to look at the commented example in
       [bonsai/examples/example_bonsai_config_file.txt] (https://github.com/CHIMEFRB/bonsai/blob/master/examples/example_bonsai_config_file.txt)
     - Most of the example scripts in
       [rf_pipelines/examples] (https://github.com/kmsmith137/rf_pipelines/tree/master/examples)
       include a bonsai dedisperser in the pipeline, so these may be useful to look at.
       They also show how to run the bonsai code through its rf_pipelines interface, which is the most convenient way to run it.


