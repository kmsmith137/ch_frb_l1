#!/usr/bin/env python

import build_helpers

# If called recursively in superbuild, a global persistent HeavyHandedUninstaller will be returned.
u = build_helpers.get_global_heavy_handed_uninstaller()

u.uninstall_headers('ch_frb_l1.hpp')
u.uninstall_headers('l1-rpc.hpp')
u.uninstall_headers('rpc.hpp')
u.uninstall_executables('ch-frb-*')
u.uninstall_executables('rpc-client')
u.uninstall_pyfiles('simulate_l0.so')

# If called recursively in superbuild, run() will not be called here.
if __name__ == '__main__':
    u.run()
