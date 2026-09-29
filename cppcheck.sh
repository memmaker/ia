#/usr/bin/env sh

cppcheck \
        --enable=all \
        --disable=style \
        --verbose \
        --project=./build/compile_commands.json \
        --std=c++17 \
        --suppressions-list=.cppcheck-suppressions \
        --output-format=text
