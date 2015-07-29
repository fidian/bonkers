BONKERS!
========

Ever wish you could trigger actions by slamming your fist against a mighty Button of Doom?  Bonkers is a lightweight C program to monitor this style of devices.  When the button is pressed you are able to run command-line programs.  A little creativity and you can have your lights turn off, a thunderclap come from your speakers and computers.  You can use one of these supported devices:

* Dream Cheeky - Big Red Button (`1d34:000d`)
* Dream Cheeky - Stress Ball (`1d34:0020`)
* Dream Cheeky - USB Fidget (`1d34:0001`)
* EB Brands (E&B Giftware, LLC) - USB ! Key (`1130:6626`)
* I'd happily accept patches for others!

[![Big Red Button](doc/big-red-button/thumb.jpg)](doc/big-red-button/image.jpg)[![USB ! Key](doc/usb-exclamation-key/thumb.jpg)](doc/usb-exclamation-key/image.jpg)[![Stress Ball](doc/stress-ball/thumb.jpg)](doc/stress-ball/image.jpg)


Getting Started
---------------

This guide breaks down the installation procedure into very small steps.  When possible, I've included information for how to test that things are installed correctly.


### Prerequisites

Once compiled, requires `libusb-1.0` installed, which is often installed by default.

The development package is necessary for compilation.  On Debian/Ubuntu systems you use `sudo apt-get install libusb-1.0-0-dev`.  Similar packages exist for other flavors of Linux/Unix.

To make sure this worked, the file `/usr/include/libusb-1.0/libusb.h` should exist on your system.


### Device Permissions with udev

This step is optional.  It helps to have the permissions on the device changed when you plug it in.  This will let Bonkers communicate with the device without needing to run as root.  If you do not do this, you must use `sudo` or somehow run Bonkers with the right permissions to read the file.

Adding these lines is the easy way to do things.

1. As root, copy [`doc/99-bonkers.rules`](doc/99-bonkers.rules) to `/etc/udev/rules.d`.
2. Run `udevadm control --reload-rules`

Now we should test that it is working as expected.

1. Plug in a supported device.
2. Run `lsusb` and note the bus and device numbers.
3. Run `ls -l /dev/bus/usb/BUS_NUMBER/DEVICE_NUMBER` and confirm the permissions are set properly.

For reference, this is the one line of output from `lsusb` we care about when using the Big Red Button:

    Bus 003 Device 021: ID 1d34:000d Dream Cheeky Dream Cheeky Big Red Button

The important bits are the bus number and device number.  This means it created a node under `/dev/bus/usb/003/021`.  Simply use `ls -l /dev/bus/usb/003/021` (change the filename to match yours) and you should see something like this.

    crw-rw-rw- 1 root root 189, 276 Mar 27 17:30 /dev/bus/usb/003/021

You want it to say the "crw-rw-rw-" on the left.  If the mode matches then the udev rule was applied successfully.


### Compile

Check out the repository and `cd` into it.  Now start the build.

    make

It should have created a file called `bonkers`.  Let's test it out by running it.

    ./bonkers -v

This will start the program and will report events to the console.  The `-v` makes it very verbose as to what's going on.  There's a few nice features you can enable; check them out by running `./bonkers -h`.  Stop the program by pressing control-C.


Running Commands
----------------

Let's start with something easy.  Just echoing the result of what's happening, but disabling all other non-error output.

    ./bonkers -q -c "echo 'STATUS CHANGE'"

Run this for a bit and you'll see different messages.  Again, press control-C to quit.  My output for a Big Red Button (open the lid, press the button, stop pressing the button, close the lid) looks like this.

    STATUS CHANGE 15 00
    STATUS CHANGE 17 15
    STATUS CHANGE 16 17
    STATUS CHANGE 17 16
    STATUS CHANGE 15 17

Because I have a Big Red Button, I can hook into one of the example scripts in the repository.  Let's run this again with that script.

    ./bonkers -q -c examples/big-red-buton.sh

And the output from the same procedure:

    System is safe
    Weapons are armed - proceed with caution
    FIRE FIRE FIRE
    Cease fire
    System is safe


### Tying into udev

If you like, you can modify your `/etc/udev/rules.d/99-bonkers.rules` to run a command of your choice.  Let's copy `examples/udev.sh` and `bonkers` to `/usr/local/bin/`.  You'll likely need root privileges.

    sudo cp examples/udev.sh /usr/local/bin/
    sudo cp bonkers /usr/local/bin/

Now edit `/usr/local/bin/udev.sh` as root and make sure it does what you want.  After that, edit `/etc/udev/rules.d/99-bonkers.rules`, look for your device and add a `RUN` section, like this:

    # Dream Cheeky - Big Red Button
    ACTION=="add", SUBSYSTEM=="usb", ATTR{idVendor}=="1d34", ATTR{idProduct}=="000d", MODE="0666", RUN+="/usr/local/bin/udev.sh"


Device Specific Information
---------------------------

Each device seems to want to send its own codes, so Bonkers will standardize them for you.  Also, repeated codes are not sent to your command; you are notified only when values change.


### Dream Cheeky - USB Fidget

I don't have one of these yet so I am unable to thoroughly test that this type of button works.

Arguments passed to command:

1. Current button value (0 when not pressed, 1 when pressed)
2. Previous button value (0 or 1) - omitted on first call

Examples:

    # First call:
    your_command 0

    # Button pressed:
    your_command 1 0

    # Button released:
    your_command 0 1

Compiling bonkers and running `./bonkers -q -c examples/fidget.sh` will report when you press and release the button.


### Dream Cheeky - Big Red Button

Arguments passed to command:

1. Current button state (0 when not pressed, 1 when pressed)
2. Current lid state (0 when closed, 1 when open)
3. Previous button state (0 or 1) - omitted on first call
4. Previous lid state (0 or 1) - omitted on first call

Examples:

    # First call:
    your_command 0 0

    # Lid opened:
    your_command 0 1 0 0

    # Button pressed:
    your_command 1 1 0 1

    # Button released:
    your_command 0 1 1 1

    # Lid closed:
    your_command 0 0 0 1

Compiling bonkers and running `./bonkers -q -c examples/big-red-button.sh` will explain the different states and let you experiment with the device.


### Dream Cheeky - Stress Ball

The command that this executes is called **EXTREMELY** often.  It will be called several times per second because the device is digitizing analog sensors and there's a continual wobble in the conversion.  It would be wise to make it run as fast as possible.  You will also want to manually debounce if this is used to trigger effects.

Arguments passed to command:

1. Current squeeze sensor reading (0 to 255)
2. Current twist sensor reading (0 to 255)
3. Current push/pull sensor reading (0 to 255)
4. Previous squeeze sensor reading (0 to 255) - omitted on first call
5. Previous twist sensor reading (0 to 255) - omitted on first call
6. Previous push/pull sensor reading (0 to 255) - omitted on first call

Examples:

    # First call:
    your_command 108 142 186

    # Subsequent calls:
    your_command 109 145 187 108 142 186
    your_command 108 145 189 109 145 187
    your_command 108 144 189 108 145 189

Compiling bonkers and running `./bonkers -q -c examples/stress-ball.sh` will show the sensor values and graphs.


### EB Brands - USB ! Key

When pressed, this button fires events.  It does not continually fire events while held; there is no way to differentiate between a held button and a momentarily pressed button.

Additionally, this button does not appear to respond well to rapid-fire pressing and will only trigger the event occasionally.  Typically that would still be acceptable.

Arguments passed to command:

1. Current button state (0 when not pressed, 1 when pressed)
2. Previous button state (0 or 1) - omitted on first call

Examples:

    # First call:
    your_command 0

    # Pressing the button:
    your_command 1 0
    your_command 0 1

Compiling bonkers and running `./bonkers -q -c examples/usb-exclamation-key.sh` will report when you press the button.


License
-------

This code is licensed under an MIT license with an additional non-advertising clause.  Read the full text in [LICENSE.md](LICENSE.md).


Thanks!
-------

* Malcom Sparks authored an article for [OpenSensors.IO](http://blog.opensensors.io/blog/2013/11/25/the-big-red-button/) that explained a lot about the button and provided source code.
* Arran Cudbard-Bell wrote [big_red_button.c](https://gist.github.com/arr2036/9932438), which uses libusb.
* Derrick Spell wrote [dream-cheeky](https://github.com/derrick/dream_cheeky), which supported the USB Fidget.
* Jan Axelson's [generic HID example](http://www.microchip.com/forums/m340898.aspx) under Linux with libusb.
* [Wireshark](https://www.wireshark.org/) for sniffing USB.
