#!/usr/bin/env python
#
# This is a toy L1b script, intended as a way of documenting the L1a-L1b interface.
# For example usage, see "Quick-start examples which can run on a laptop" in MANUAL.md.
#
# This script receives coarse-grained triggers from L1a, and "processes" them by generating
# a big waterfall plot (toy_l1b_beam${BEAM_ID}.png).
#
# Although toy-l1b.py is intended as documentation, it may also be useful for debugging,
# since it provides a simple way of plotting the output of L1a.
#
# The bonsai.PipedDedisperser class is used to receive data from L1a.  For another
# toy example illustrating use of this class (outside the L1 server framework), see 
# examples_python/example4*.py in the bonsai repository.

import sys
import bonsai
import itertools
import numpy as np


# When the L1 server spawns its L1b subprocesses, it uses the command line:
#
#   <l1b_executable_filename> <l1b_config_filename> <beam_id>
#
# The l1b_config_filename is specified on the command line when the L1 server
# is started.  It is "opaque" to the L1 server, and just gets passed along via
# the L1b command line.  Since this toy L1b script doesn't need any config
# information, we just ignore it here.

assert len(sys.argv) == 3

l1b_config_filename = sys.argv[1]
beam_id = int(sys.argv[2])

print 'toy-l1b.py: starting, config_filename="%s", beam_id=%d' % (l1b_config_filename, beam_id)

# When the L1 server spawns its L1b subprocess, it creates a unix pipe (which replaces
# 'stdin' for the child process) which will be used to send coarse-grained triggers, and
# bonsai config_params.  The bonsai.PipedDedisperser python class will read and decode data
# sent over the pipe.  For more information see the bonsai.PipedDedisperser docstring, or
# bonsai/examples_python/example4*.py.

dedisp = bonsai.PipedDedisperser()

# 'dedisp.config' is a Python dictionary containing all of the bonsai config_params
# (ntrees, tree_size, etc.)  For a list of all config_params, see the bonsai documentation,
# in particular the section "Bonsai config file: syntax reference" in bonsai/MANUAL.md.
#
# Note that many config params are also available as PipedDedisperser properties, e.g.
# dedisp.ntrees is the same as dedisp.config['ntrees'].
#
# print dedisp.config


#
# This is the main receive loop, which gets triggers from L1a.
#
# In this toy code, we just append triggers to a list, and process them
# after L1a exits.
#

all_triggers = [ ]

for ichunk in itertools.count():
    # The return value from get_triggers() is list of length dedisp.ntrees.
    #
    # The i-th element of this list is a 4D numpy array with shape (dedisp.ndm_coarse[i], 
    # dedisp.nsm[i], dedisp.nbeta[i], dedisp.nt_coarse_per_chunk[i]).

    t = dedisp.get_triggers()

    if t is None:
        print 'toy-l1b.py: last trigger chunk received, exiting'
        break

    print 'toy-l1b.py: received (beam_id,chunk_id) = (%d,%d)' % (beam_id, ichunk)

    # Just for fun, a sanity check here
    assert len(t) == dedisp.ntrees
    for (i,a) in enumerate(t):
        assert a.shape == (dedisp.ndm_coarse[i], dedisp.nsm[i], dedisp.nbeta[i], dedisp.nt_coarse_per_chunk[i])

    # A quick-and-dirty way to prevent the final plot from getting too large.
    if len(all_triggers) < 128:
        all_triggers.append(t)


########################################  plotting code starts here  ########################################
#
# Now that all triggers have been received, we write plots.
#
# The following plotting code was hacked together by cutting-and-pasting
# from rf_pipelines.
#


try:
    import PIL.Image
except:
    print >>sys.stderr, 'toy-l1b.py: import PIL.Image failed, no plot will be written'
    sys.exit(0)

nchunks = len(all_triggers)
nxpix_per_chunk = np.max(dedisp.nt_coarse_per_chunk)
nxpix_tot = nchunks * nxpix_per_chunk

if not all((nxpix_per_chunk % nt == 0) for nt in dedisp.nt_coarse_per_chunk):
    print >>sys.stderr, 'toy-l1b.py: hacked-together plotting code assumes that all nt_coarse_per_chunk values are divisible by each other, no plot will be written'
    sys.exit(0)

ntrees = dedisp.ntrees
base_ypix_list = np.zeros(ntrees, dtype=np.int)
nypix_list = np.zeros(ntrees, dtype=np.int)
nypix_tot = 10

for i in xrange(dedisp.ntrees):
    base_ypix_list[i] = nypix_tot
    nypix_list[i] = 2 * dedisp.ndm_coarse[i]
    nypix_tot += nypix_list[i] + 10


def triggers_rgb(arr, out_shape, threshold1=6, threshold2=10):
    assert 0 < threshold1 < threshold2

    assert arr.ndim == 2
    (ndm, nt) = arr.shape

    # In the output RGB array, we put time on the x-axis (index 0) and DM on the y-axis (index 0).
    arr = np.transpose(arr)
    assert out_shape[0] % arr.shape[0] == 0
    assert out_shape[1] % arr.shape[1] == 0

    # 2D boolean arrays
    below_threshold1 = (arr < threshold1)
    below_threshold2 = (arr < threshold2)

    # below threshold1: scale range [0,threshold1] -> [0,1]
    t0 = arr / threshold1
    t0 = np.maximum(t0, 0.0001)    # 0.0001 instead of 0.0, to make roundoff-robust
    t0 = np.minimum(t0, 0.9999)    # 0.9999 instead of 1.0, to make roundoff-robust

    # below threshold1: use (dark blue) -> (dark red) scale, same as write_png()
    # between threshold1 and threshold2: yellow (r=g=255, b=51)
    # above threshold2: green (r=b=100, g=255)

    rgb = np.zeros((arr.shape[0], arr.shape[1], 3), dtype=np.uint8)
    rgb[:,:,0] = np.where(below_threshold1, 256*t0,     np.where(below_threshold2, 255, 100))
    rgb[:,:,1] = np.where(below_threshold1, 0,          np.where(below_threshold2, 255, 255))
    rgb[:,:,2] = np.where(below_threshold1, 256*(1-t0), np.where(below_threshold2, 51, 100))

    # The following chain of steps upsamples the rgb array: 
    # (arr.shape[0], arr.shape[1], 3) -> (out_shape[0], out_shape[1], 3).

    nups_x = out_shape[0] // arr.shape[0]
    nups_y = out_shape[1] // arr.shape[1]
    rgb = np.reshape(rgb, (arr.shape[0], 1, arr.shape[1], 1, 3))
    rgb2 = np.zeros((arr.shape[0], nups_x, arr.shape[1], nups_y, 3), dtype=np.uint8)
    rgb2[:,:,:,:,:] = rgb[:,:,:,:,:]
    rgb2 = np.reshape(rgb2, (out_shape[0], out_shape[1], 3))

    return rgb2


monster_plot = np.zeros((nxpix_tot, nypix_tot, 3), dtype=np.uint8)
monster_plot[:,:,:] = 255


for (ichunk, trigger_list) in enumerate(all_triggers):
    for (itree, trigger_arr) in enumerate(trigger_list):
        (ndm, nt) = (dedisp.ndm_coarse[itree], dedisp.nt_coarse_per_chunk[itree])

        # coarse-grain over sm, beta axes
        trigger_arr = np.max(trigger_arr, axis=2)
        trigger_arr = np.max(trigger_arr, axis=1)
        assert trigger_arr.shape == (ndm, nt)

        ix = ichunk * nxpix_per_chunk
        nx = nxpix_per_chunk
        iy = base_ypix_list[itree]
        ny = nypix_list[itree]

        rgb_tile = triggers_rgb(trigger_arr, out_shape=(nx,ny))
        monster_plot[ix:(ix+nx),iy:(iy+ny),:] = rgb_tile


# Note: PIL's conventions are reversed relative to ours in both cases:
#   - PIL axis ordering is (y,x)
#   - PIL y-axis direction is top-to-bottom
#
# so transpose array and 

monster_plot = np.transpose(monster_plot, axes=(1,0,2))
monster_plot = monster_plot[::-1,:,:]

filename = 'toy_l1b_beam%d.png' % beam_id
img = PIL.Image.fromarray(monster_plot)
img.save(filename)
print 'toy-l1b.py: wrote %s' % filename
