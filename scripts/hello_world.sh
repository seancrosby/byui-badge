#!/bin/bash

cd projects/hello_world
idf.py fullclean
idf.py set-target esp32s3
idf.py build flash monitor
