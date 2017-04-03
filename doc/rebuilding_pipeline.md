Broken pipeline?  The following sequence of commands will put all modules on their master branches,
update from git, and rebuild everything.
```
cd simd_helpers
git checkout master
git pull
make -j4 install
cd ..

cd sp_hdf5
git checkout master
git pull
make -j4 install
cd ..

cd simpulse
make clean uninstall
git checkout master
git pull
make -j4 install
cd ..

cd ch_frb_io
make clean uninstall
git checkout master
git pull
make -j4 install
cd ..

cd bonsai
make clean uninstall
git checkout master
git pull
make -j4 install
cd ..

cd rf_pipelines
make clean uninstall
git checkout master
git pull
make -j4 install
cd ..

cd ch_frb_rfi
make clean uninstall
git checkout master
git pull
make -j4 install
cd ..

cd ch_frb_l1
make clean
git checkout master
git pull
make -j4 all
cd ..
```
