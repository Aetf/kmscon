#!/bin/sh

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 argument"
    exit 1
fi

# Store current tty number
if [ -d /sys/class/tty/tty0 ]; then
    kms_tty=$(cat /sys/class/tty/tty0/active)
fi

printf "\x1B]setBackground\a"
eval "${@:1}"

# If the current tty has changed, wait until the user switches back.
if [ -n "${kms_tty}" ]; then
    while [ "$(cat /sys/class/tty/tty0/active)" != "${kms_tty}" ]; do
        sleep 1
    done
fi

printf "\x1B]setForeground\a"
