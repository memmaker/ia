#!/usr/bin/env sh

# Script used for normalizing .ogg audio files to 44100 Hz.
#
# NOTE: Requires ffmpeg.
#
# E.g. "sudo apt install ffmpeg"
#
#
# NOTE: To see information from an audio file, run:
#
#  ffprobe -i <filename>
#

set -eux

files=$*

for f in ${files}; do
    tmp_file=tmp.$(basename $f)
    ffmpeg -i $f -ar 44100 -ac 1 ${tmp_file} && mv ${tmp_file} $f
done
