#!/bin/sh

set -u

PNP='pnp-server'
CompileOpts='-I ../include/TinyBase/src -I ../include/TinyServer/src -Wall -mavx2 -fpermissive -lm -lpthread -w'
Fast='-O2'
Debug='-g'

mkdir -p ../build
cd ../build
g++ -o ${PNP} ../src/${PNP}.cpp ${CompileOpts} ${Fast}
g++ -o ${PNP}-debug ../src/${PNP}.cpp ${CompileOpts} ${Debug}
cd ../src