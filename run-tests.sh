#!/usr/bin/env sh

set -xue

root_dir=${PWD}

./build-tests.sh

cd build

# Define as "--use-colour=no" do disable colors
color_opt=""

./ia-test ${color_opt} -D 3 --abort "$*"

cd ${root_dir}
