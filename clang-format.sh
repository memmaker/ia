#!/usr/bin/env sh

set -eu

files=$(find include/ src/ test/include/ test/src/ test/test_cases/ -type f -name '*.*pp') 

clang-format -i -style=file ${files}
