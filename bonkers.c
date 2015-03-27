/**
 * BONKERS!
 *
 * Detect button presses from USB devices, such as the Big Red Button and
 * USB Fidget.
 *
 * @site https://github.com/fidian/bonkers
 * @license MIT (LICENSE.md)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <libusb-1.0/libusb.h>

#define DEBUG(_fmt, ...) if (output_level > 1) fprintf(stdout, "DEBUG: "_fmt "\n", ## __VA_ARGS__)
#define INFO(_fmt, ...) if (output_level > 0) fprintf(stdout, _fmt "\n", ## __VA_ARGS__)
#define ERROR(_fmt, ...) fprintf(stderr, _fmt "\n", ## __VA_ARGS__)


static int output_level = 1;  // 0 = none, 1 = info, 2 = debug
static int exit_code = -1;  // -1 = run, 0 = success, 1+ = error

/**
 * Finds a device when given a vendor_id and product_id.
 *
 * name = Name of device for debug message
 * vendor_id = Vendor ID of USB device
 * product_id = Product ID of USB device
 *
 * Returns the handle or NULL
 */
static struct libusb_device_handle *get_button_handle(char *name, int vendor_id, int product_id) {
    struct libusb_device_handle *handle = NULL;

    DEBUG("Attempting to open %s (vendor 0x%04x, device 0x%04x)", name, vendor_id, product_id);
    handle = libusb_open_device_with_vid_pid(NULL, vendor_id, product_id);

    return handle;
}


/**
 * Detaches the kernel driver if it is currently attached.
 *
 * handle = USB device
 *
 * returns 0 on success, 1 on failure.
 */
static int detach_kernel_driver(libusb_device_handle *handle) {
    int ret;

    /* If the kernel driver is active, we need to detach it */
    if (libusb_kernel_driver_active(handle, 0)) {
        DEBUG("Kernel driver active, attempting to detach");
        ret = libusb_detach_kernel_driver(handle, 0);

        if (ret < 0) {
            return 1;
        }
    } else {
        DEBUG("Kernel driver not active");
    }

    return 0;
}


/**
 * Sets the button up so we can read data from it.
 *
 * handle = USB device
 *
 * Returns 0 on success, -1 on error.
 */
static int set_button_control(struct libusb_device_handle *handle) {
    int ret;
    uint8_t state[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 };

    ret = libusb_control_transfer(handle, 0x21, 0x09, 0x00, 0x00, state, 8, 0);

    if (ret < 0) {
        ERROR("Error reading response %i", ret);

        return -1;
    }

    if (ret == 0) {
        ERROR("Device didn't send enough data");

        return -1;
    }

    return 0;
}


/**
 * Attempt to read the current button state.
 *
 * handle = USB device
 * state = where to copy the resulting value on success
 *
 * Returns -1 on hard error, 0 on success, 1 on soft error.
 */
static int read_button_state(struct libusb_device_handle *handle, uint8_t *state) {
    uint8_t data[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    int dev[8];
    int ret;

    ret = set_button_control(handle);

    if (ret < 0) {
        // Hard error
        return -1;
    }

    /* Send 0x81 to the EP to retrieve the state */
    ret = libusb_interrupt_transfer(handle, 0x81, data, 8, dev, 200);

    if (ret < 0) {
        // Soft error
        DEBUG("Error getting interrupt data - ignoring");

        return 1;
    }

    *state = data[0];

    return 0;
}


/**
 * Repeatedly try to read the button state, pausing between reads.
 *
 * If a hard error is reported or there is success, this function exits.
 *
 * handle = USB device
 * dest = where to store the resulting byte
 * interval = how long to wait between soft errors and trying again
 *
 * returns 0 on success, <0 on hard error, >0 on soft error
 */
static int repeat_read_button_state(libusb_device_handle *handle, uint8_t *dest, int interval) {
    int result;

    while (exit_code == -1) {
        result = read_button_state(handle, dest);

        if (result <= 0) {
            return result;
        }

        usleep(interval);
    }

    return 1;
}


/**
 * Run a command.  This also adds the current and previous status codes
 * as command-line arguments.
 *
 * cmd = command to run (we add two more arguments)
 * now = current status
 * then = previous status
 */
void run_command(char const *cmd, uint8_t now, uint8_t then) {
    char *modified;
    int ret, bytes;

    if (cmd) {
        bytes = strlen(cmd) + 6;
        modified = malloc(bytes);

        if (! modified) {
            ERROR("Could not allocate memory for command (%d bytes)", bytes);
            exit_code = 1;

            return;
        }

        sprintf(modified, "%s %02x %02x", cmd, now, then);
        DEBUG("Running command: %s", modified);
        ret = system(modified);
        DEBUG("Command returned %i", ret);
        free(modified);
    }
}


/**
 * Set a flag to clean up gracefully.
 */
void exit_handler(int sig_num) {
    exit_code = 0;
}


/**
 * Help text
 *
 * name = the name of the program from the command line
 */
static void usage(char *name) {
    printf(
    "BONKERS!\n"
    "\n"
    "For more information, see the website:\n"
    "    https://github.com/fidian/bonkers\n"
    "\n"
    "Usage: %s [options]\n"
    "  -c <command>      Command to execute with current and previous status.\n"
    "  -h                This help text.\n"
    "  -p <microsends>   Polling interval.\n"
    "  -q                Quiet - silences output.\n"
    "  -v                Turn on verbose output.\n",
    name
    );
}


/**
 * Run the detection loop.  This repeatedly polls the device for a status.
 *
 * This loop function must watch exit_code, terminating if it is not -1.
 *
 * handle = the device
 * interval = usleep time
 * command = command to execute on status updates
 */
static void run_detector(libusb_device_handle *handle, int interval, const char *command) {
    uint8_t then = 0, now;

    // Poll the device to get the status until SIGINT or hard error
    while (exit_code == -1) {
        if (repeat_read_button_state(handle, &now, interval)) {
            exit_code = 1;

            return;
        }

        if (then != now) {
            INFO("State switched from %02x to %02x", then, now);
            run_command(command, now, then);
            then = now;
        }

        usleep(interval);
    }
}


/**
 * Use getopt to parse the command-line arguments.
 *
 * argc, argv = same as main
 * interval = where to set an interval if one is passed
 * command = where to assign the pointer for the command to execute
 */
void parse_arguments(int argc, char **argv, int *interval, const char **command) {
    char c;

    while ((c = getopt(argc, argv, "c:hp:qv")) != EOF) {
        switch (c) {
            case 'c':
                *command = optarg;
                break;

            case 'h':
                usage(argv[0]);
                exit_code = 0;

                return;

            case 'p':
                *interval = atoi(optarg);

                if (*interval <= 0) {
                    exit_code = 1;

                    return;
                }

                break;

            case 'q':
                output_level = 0;
                break;

            case 'v':
                output_level = 2;
                break;
        }
    }
}


/**
 * Our program
 */
int main(int argc, char **argv) {
    char const *command = NULL;
    int interval = 20000;
    struct libusb_device_handle *handle = NULL;

    // Handle arguments to our program
    parse_arguments(argc, argv, &interval, &command);

    // In case any arguments were invalid
    if (exit_code != -1) {
        return exit_code;
    }

    // Setup a signal handler, so we can cleanup gracefully
    signal(SIGINT, exit_handler);

    // Initialise libusb (with the default context)
    libusb_init(NULL);

    // Try to get a handle for each supported device
    handle = get_button_handle("Dream Cheeky - Big Red Button", 0x1d34, 0x000d);

    if (!handle) {
        handle = get_button_handle("Dream Cheeky - USB Fidget", 0x1d34, 0x0001);
    }

    if (!handle) {
        ERROR("Failed opening device descriptor (you may need to be root)...");

        return 1;
    }

    // Detach the kernel driver if it is attached
    if (detach_kernel_driver(handle)) {
        ERROR("Can't detach kernel driver");

        return 1;
    }

    // Run the detector - this polls the device in a loop
    run_detector(handle, interval, command);

    // We are done
    DEBUG("Closing USB");
    fflush(stdout);
    fflush(stderr);
    libusb_close(handle);

    return exit_code;
}
