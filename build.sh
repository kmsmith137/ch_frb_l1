#! /bin/bash

# stop on any error
set -e

# echo commands
set -x

# check out repositories

if [ ! -d simd_helpers ]; then
  git clone https://github.com/kmsmith137/simd_helpers.git
fi
if [ ! -d sp_hdf5 ]; then
  git clone https://github.com/kmsmith137/sp_hdf5.git
fi
if [ ! -d simpulse ]; then
  git clone https://github.com/kmsmith137/simpulse.git
fi
if [ ! -d ch_frb_io ]; then
  git clone https://github.com/CHIMEFRB/ch_frb_io.git
fi
if [ ! -d bonsai ]; then
  git clone https://github.com/CHIMEFRB/bonsai.git
fi
if [ ! -d rf_pipelines ]; then
  git clone https://github.com/kmsmith137/rf_pipelines.git
fi
if [ ! -d ch_frb_rfi ]; then
  git clone https://github.com/mrafieir/ch_frb_rfi.git
fi
if [ ! -d ch_frb_l1 ]; then
  git clone https://github.com/kmsmith137/ch_frb_l1
fi
if [ ! -d toy_network_codes ]; then
  git clone https://github.com/kmsmith137/toy_network_codes
fi

# Setup remote branches -- HACKily

(cd simpulse;
    git remote add dstn https://github.com/dstndstn/simpulse.git || true;
    git fetch dstn;
    git checkout --track -b travis dstn/travis || true;
)

(cd rf_pipelines;
    git remote add dstn https://github.com/dstndstn/rf_pipelines.git || true;
    git fetch dstn;
    git checkout --track -b hf5conv-2+v15 dstn/hf5conv-2+v15 || true;
)


# Setup Makefile.local links

(cd simd_helpers;
    if [ ! -e Makefile.local ]; then
	ln -s site/Makefile.local.norootprivs Makefile.local;
    fi
)

(cd simpulse;
    if [ ! -e Makefile.local ]; then
	ln -s site/Makefile.local.travis Makefile.local;
    fi
)

(cd sp_hdf5;
    if [ ! -e Makefile.local ]; then
	ln -s site/Makefile.local.linux Makefile.local;
    fi
)

(cd ch_frb_io;
    if [ ! -e Makefile.local ]; then
	ln -s site/Makefile.local.frb1 Makefile.local;
    fi
)

(cd bonsai;
    if [ ! -e Makefile.local ]; then
	ln -s site/Makefile.local.frb1 Makefile.local;
    fi
)

(cd rf_pipelines;
    if [ ! -e Makefile.local ]; then
	ln -s site/Makefile.local.travis Makefile.local;
    fi
)

(cd ch_frb_rfi;
    if [ ! -e Makefile.local ]; then
	ln -s site/Makefile.local.frb1 Makefile.local;
    fi
)

(cd ch_frb_l1;
    if [ ! -e Makefile.local ]; then
	ln -s site/Makefile.local.frb1 Makefile.local;
    fi
)

# Set build variables
export INCDIR=~/include
export LIBDIR=~/lib
export BINDIR=~/bin
export PYDIR=~/lib/python2.7/site-packages
export PYTHON_INCDIR=/usr/include/python2.7
export NUMPY_INCDIR=/usr/lib64/python2.7/site-packages/numpy/core/include
# MSGPACK_INC_DIR
# ZMQ_INC_DIR
# ZMQ_LIB_DIR
# JSONCPP_INC_DIR
export JSONCPP_INCDIR=/usr/include/jsoncpp
export HAVE_PSRFITS=n
export HAVE_CH_FRB_IO=y
export HAVE_BONSAI=y

# Build!

(cd simd_helpers &&
    git pull &&
    git checkout v4_devel &&
    make &&
    make install
)

(cd simpulse &&
    git pull &&
    git checkout travis &&
    make &&
    make install
)

(cd sp_hdf5 &&
    git pull &&
    git checkout master &&
    make &&
    make install
)

(cd ch_frb_io &&
    git pull &&
    git checkout master &&
    make &&
    make install
)

(cd bonsai &&
    git fetch &&
    git checkout v10_devel &&
    make &&
    make install)

(cd rf_pipelines &&
    git fetch &&
    git checkout hf5conv-2+v15 &&
    make &&
    make install)

(cd ch_frb_rfi &&
    git fetch &&
    git checkout master &&
    make install)
    
(cd ch_frb_l1 &&
    git fetch &&
    git checkout master &&
    make)

(cd toy_network_codes &&
        git fetch &&
        git checkout master &&
        make &&
        make install)
