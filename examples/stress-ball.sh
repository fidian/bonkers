#!/bin/sh
#
# Example for reporting the sensors of the Dream Cheeky "Stress Ball"

graph() {
    echo "$1: $2"

    # Scale 0-255 down to 0-64
    NUM=$(($2 / 4))

    while [ $NUM -gt 0 ]; do
        echo -n "*"
        NUM=$(($NUM - 1))
    done

    echo ""
}

clear
graph squeeze "$1"
graph twist "$2"
graph push/pull "$3"
