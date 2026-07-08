#!/bin/bash

./run.pl opencv_example --camera $1
./run.pl opencv_model --camera $1 --filename `pwd`/models/obj/cube.obj
./run.pl compute_shader --camera $1

