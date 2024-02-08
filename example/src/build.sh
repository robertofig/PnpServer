#!/bin/sh

set -u

PRIME='prime'
MODIFY='modify'
CompileOpts='-I ../../src -Wall -mavx2 -fpermissive -lm -w -O2 -fPIC -shared'

mkdir -p ../prime
gcc -o ../prime/${PRIME}.so ${PRIME}.c ${CompileOpts}
cp ./primes ../prime/primes

mkdir -p ../modify
g++ -o ../modify/${MODIFY}.so ${MODIFY}.cpp ${CompileOpts}