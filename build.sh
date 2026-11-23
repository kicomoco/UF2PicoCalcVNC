#!/bin/sh
# Builds build/picocalc_vnc.uf2
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ls -l build/picocalc_vnc.uf2
