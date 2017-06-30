#!/usr/bin/env python

import sys
import time
import rf_pipelines

s = rf_pipelines.chime_network_stream(udp_port=6677)

# Inject a 20 sigma FRB.
#
# Note 1: in the simpilified bonsai config used in this subscale pipeline, the max DM of the search is 103!
#
# Note 2: for the frb_injector_transform to correctly normalize the pulse to have a specified "target" snr,
#   it needs to know the variance of the intensity data.  In the real pipeline we keep a running per-frequency-channel
#   estimate of the variance.  Here we "cheat" and use the true variance of the simulation.  The ch-frb-simulate-l0
#   code uses an arbitrary normalization, whose variance happens to be 2133.33.

print 'Note: a simulated FRB will be added to the data received from L0!'
t0 = rf_pipelines.frb_injector_transform(snr = 20.0,
                                         undispersed_arrival_time = 5.0,
                                         dm = 50.0,
                                         variance = 2133.33)

t1 = rf_pipelines.plotter_transform(img_prefix = 'waterfall',
                                    img_nfreq = 256,
                                    img_nt = 256,
                                    downsample_nt = 16,
                                    n_zoom = 1)

# Warning: The bonsai code assumes that some form of detrending (high-pass filtering) has been done,
# otherwise it will generate unpredictable results!  Here we use a very simple detrender which subtracts
# the mean of every frequency channel, in 1024-time-sample "chunks".
t2 = rf_pipelines.polynomial_detrender(deg=0, axis=0, nt_chunk=1024)

t3 = rf_pipelines.bonsai_dedisperser(config_filename = 'subscale_bonsai_config.txt',
                                     img_prefix = 'triggers',
                                     track_global_max = True,
                                     downsample_nt = 16)

s.run([t0,t1,t2,t3],    # list of transforms
      outdir = time.strftime('subscale_run_%y-%m-%d-%X'))

