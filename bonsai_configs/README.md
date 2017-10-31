Important note #1: "Production" bonsai_configs (i.e. filenames beginning with "bonsai_production_...")
are intended to be "set in stone" after they are committed to git.  If you want to change parameters 
in a production config file, make a new copy of the file and increment the version number!

Important note #2: The production bonsai_configs are kept in three places:

   - in the ch_frb_l1 repository
   - in the ch_frb_rfi repository
   - in `/data/bonsai_configs` on the CHIME nodes (frb1, frb-compute-0, etc.)  

It is important to make sure that the three versions don't get out of sync!  This should
happen automatically if production configs are never changed after committing them to git.

Reminder: currently, bonsai config files must be constructed by a two-step process as follows.
First, a human-editable text file `bonsai_xxx.txt` is written.  Second, this is "compiled"
into an HDF5 file using the utility `bonsai-mkweight`.
```
bonsai-mkweight bonsai_xxx.txt bonsai_xxx.hdf5
```
where the `bonsai-mkweight` utility is part of bonsai, not ch_frb_l1.

Note that we don't put the HDF5 files in git, since they are large files, so you may need
to create the hdf5 files by hand using the above procedure.  Exception: on the CHIME nodes, 
the "production" HDF5 files should already be in /data/bonsai_configs.

The production bonsai configs differ in number of trial spectral indices, and whether 
an upsampled tree is used to improve signal-to-noise for narrow pulses (<~ 1 ms) at DM <= 820.

   - params_noups_nbeta1.txt: no upsampled tree, 1 trial spectral index.
   - params_noups_nbeta2.txt: no upsampled tree, 2 trial spectral indices.  (*)
   - params_ups_nbeta1.txt:   upsampled tree, 1 trial spectral index.       (*) 
   - params_ups_nbeta2.txt:   upsampled tree, 2 trial spectral indices.     (*)

(*) Important note #3: currently, RFI removal is running slower than originally hoped (0.55 cores/beam).
    This means that only the first production config (params_noups_nbeta1.txt) will actually work!
    Otherwise, the L1 server will run too slow, and eventually crash!
