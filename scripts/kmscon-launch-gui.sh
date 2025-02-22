#!/bin/sh

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 argument"
    exit 1
fi

printf "\x1B]drmDropMaster\a"
eval "${@:1}"
printf "\x1B]drmSetMaster\a"
