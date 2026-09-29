#!/usr/bin/env sh

set -xue

script_dir=$(dirname $(realpath ${0}))

cd ${script_dir}

./build-debug.sh $*

cd build

./ia-debug $*
