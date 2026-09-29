#!/usr/bin/env sh

set -xue

script_dir=$(dirname $(realpath ${0}))

cd ${script_dir}

# NOTE: "$*" allows adding extra arguments.

# NOTE: Define as -DCMAKE_EXPORT_COMPILE_COMMANDS=1 to export compile commands (this is not needed
# on cmake >= 3.20, then this is set up in CMakeLists.txt instead).
export_compile_commands_opt=""

cmake \
    -B build \
    ${export_compile_commands_opt} \
    -DIA_DEBUG_SANITIZE=0 \
    $*

cmake --build build --target ia-debug -- -j$(nproc)
