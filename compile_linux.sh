#!/bin/bash

cmake -S . -B build-linux -G Ninja
cmake --build build-linux

./build-linux/aowis-server-gui
