#!/bin/bash

./run.pl opencv_example --camera $1
./run.pl opencv_model --camera $1 --model ./models/cube.mxmod.z
./run.pl compute_shader --camera $1


