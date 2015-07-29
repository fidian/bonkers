#!/bin/sh

BUTTON_NOW=$1
LID_NOW=$2
BUTTON_PREV=$3
LID_PREV=$4

case "$BUTTON_NOW $LID_NOW $BUTTON_PREV $LID_PREV" in
    "0 0"*)
        # Button not being pressed, lid shut
        echo "System is safe"
        ;;

    "1 0"*)
        # Button being pressed, lid shut?  Weird.
        echo "Odd - cover is shut and you are pressing the button"
        ;;

    "1 1"*)
        # Button being pressed, lid open
        echo "FIRE FIRE FIRE"
        ;;

    "0 1 0 0")
        # Button not pressed, lid open
        # Previously button was not pressed, lid was closed
        echo "Weapons are armed - proceed with caution"
        ;;

    "0 1 1 0")
        # Button not pressed, lid open
        # Previously button was pressed, lid was closed
        echo "Extremely odd that you changed both things at once"
        ;;

    "0 1 1 1")
        # Button not pressed, lid open
        # Previously button was pressed, lid was open
        echo "Cease fire"
        ;;

    *)
        echo -n "Strange arguments:"
        printf " %q" "$@"
        echo ""
        ;;
esac
