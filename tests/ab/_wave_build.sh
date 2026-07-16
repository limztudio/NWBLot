#!/bin/bash
cd /home/limstudio/WorkStation/NWBLot/__cmake/build/linux-clang-namesym-x64
exec ninja -f build-opt.ninja stress_test_smoke
