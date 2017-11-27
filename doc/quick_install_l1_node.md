### QUICK-START INSTALLATION: frb-compute-X nodes

Here are instructions for building the L1 pipeline from scratch on the frb-compute-X nodex.
All external dependencies should already be installed.

Note that we don't build the ch_frb_rfi module here, since this module includes hardcoded
pathnames on frb1.physics.mcgill.ca, and probably won't be useful elsewhere.

### DIRECTORIES AND ENVIRONMENT VARIABLES
```
# Binaries, header files, libraries, and python modules will be installed in these directories.
mkdir -p ~/bin
mkdir -p ~/include
mkdir -p ~/lib
mkdir -p ~/lib/python2.7/site-packages

# Bitshuffle will be installed here.
mkdir -p ~/lib/hdf5_plugins

# I strongly recommend adding these lines to your ~/.bashrc!
# Note that '.' is added to LD_LIBRARY_PATH (the unit testing logic in most of
# the pipeline modules currently assumes this)

export LD_LIBRARY_PATH=.:$HOME/lib:/usr/local/lib:$LD_LIBRARY_PATH
export PYTHONPATH=$HOME/lib/python2.7/site-packages:$PYTHONPATH
export HDF5_PLUGIN_PATH=$HOME/lib/hdf5_plugins
```

### CHECKING OUT THE PIPELINE MODULES
```
git clone https://github.com/kiyo-masui/bitshuffle
git clone https://github.com/kmsmith137/simd_helpers
git clone https://github.com/kmsmith137/pyclops
git clone https://github.com/kmsmith137/rf_kernels
git clone https://github.com/kmsmith137/sp_hdf5
git clone https://github.com/kmsmith137/simpulse
git clone https://github.com/CHIMEFRB/ch_frb_io
git clone https://github.com/CHIMEFRB/bonsai
git clone https://github.com/kmsmith137/rf_pipelines
git clone https://github.com/kmsmith137/ch_frb_l1
```

### COMPILATION
```
cd bitshuffle
python setup.py install --user --h5plugin --h5plugin-dir=$HOME/lib/hdf5_plugins
cd ..

cd simd_helpers
ln -s site/Makefile.local.norootprivs Makefile.local
make -j20 install
cd ..

cd pyclops
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 all install
cd ..

cd rf_kernels
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 all install
cd ..

cd sp_hdf5
ln -s site/Makefile.local.linux Makefile.local
make -j20 all install
cd ..

cd simpulse
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 all install
cd ..

cd ch_frb_io
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 all install
cd ..

cd bonsai
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 all install
cd ..

cd rf_pipelines
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 all install
cd ..

cd ch_frb_l1
ln -s site/Makefile.local.frb-compute-0 Makefile.local
make -j20 all
cd ..
```
