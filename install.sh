#!/bin/bash

cmake -S . -B build $@ && cmake --build ./build -j$(nproc) && sudo cmake --install ./build

