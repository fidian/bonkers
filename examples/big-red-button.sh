#!/bin/sh

NOW=$1
THEN=$2

case "$NOW" in
    14)
        # Cover is shut, button is being pressed
        echo "Odd - cover is shut and you are pressing the button"
        ;;

    15)
        # Cover is shut, button is not being pressed
        echo "System is safe"
        ;;

    16)
        # Cover is open, button is being pressed
        echo "FIRE FIRE FIRE"
        ;;

    17)
        # Cover is open, button is not being pressed
        # Need to tell what we just got done doing
        case "$THEN" in
            00)
                echo "Careful - you might want to start with the lid closed"
                ;;

            14)
                # You would need to try HARD to get this to happen
                echo "Extremely odd that you unpressed both things at once"
                ;;

            15)
                echo "Weapons are armed - proceed with caution"
                ;;

            16)
                echo "Cease fire"
                ;;

            *)
                echo "Unknown previous state: $THEN"
                ;;
        esac
        ;;

    *)
        echo "Unknown state: $NOW"
esac
