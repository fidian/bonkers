#!/bin/bash

# Change to the same directory as this script
cd ${0%/*}

# Start Bonkers when the USB device is plugged in
# Make sure to create the script that gets ran!
./bonkers -q -c /usr/local/bin/bonkers-status-update.sh
