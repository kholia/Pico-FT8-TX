#!/bin/bash

mkdir -p build
git submodule init
git submodule update
git submodule foreach git pull origin main
cd build
cmake ..
make -j5
