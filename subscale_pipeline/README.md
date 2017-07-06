Here is a quickly-thrown-together subscale L0/L1 pipeline.  Please feel free to improve it!

  - From the `subscale_pipeline` directory, do:
    ```
    ./subscale-l1.py
    ```
    This will start a toy L1 server which expects 1 beam of data with 1024 frequency channels.
    The server is written by chaining together some python streams/transforms which do the following:

       - Receive data from L0
       - Add a 20-sigma simulated FRB with DM=50 (just for fun)
       - Waterfall-plot the intensity data
       - Dedisperse and plot the coarse-grained triggers.

  - To send 30 seconds of simulated data to the server, open another window and do:
    ```
    ../ch-frb-simulate-l0 subscale_l0.yaml 30
    ```

  - After the server exits, you should find the following png files:
    ```
    subscale_run_*/waterfall_*.png     # intensity plots
    subscale_run_*/triggers_*.png      # coarse-grained bonsai trigger plots
    ```
    The 20-sigma simulated FRB should be easy to see in `triggers_zoom0_tree0_1.0.png`.
    It is present (but buried in the noise and invisible) in `waterfall_zoom0_1.png`.
