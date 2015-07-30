#!/bin/sh

if [ "$1" = "1" ]; then
    # Current status is pressed
    echo "Button pressed"
else
    # Current status is not pressed
    # Suppress the "Button released" message if this is the first
    # call to the script.  There's no previous button state with the
    # first call.
    if [ "$2" = "1" ]; then
        echo "Button released"
    fi
fi
