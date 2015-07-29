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

#define DEBUG(_fmt, ...) if (output_level > 1) { fprintf(stdout, "DEBUG: "_fmt "\n", ## __VA_ARGS__); fflush(stdout); }
#define INFO(_fmt, ...) if (output_level > 0) { fprintf(stdout, _fmt "\n", ## __VA_ARGS__); fflush(stdout); }
#define ERROR(_fmt, ...) fprintf(stderr, _fmt "\n", ## __VA_ARGS__); fflush(stderr);

#define CONTROL_REQUEST_TYPE_IN LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE
#define CONTROL_REQUEST_TYPE_OUT LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE
#define HID_REPORT 0x09
#define HID_IDLE 0x0a

// Max number of characters in a converted state string (not including NULL)
#define MAX_STATE_STRING_LENGTH 11

typedef struct device_config {
    const char *name;
    int vendor_id;
    int product_id;
    struct libusb_device_handle *handle;
    uint16_t wValue;
    int (*read_state)(struct device_config *);
    void (*convert_state)(struct device_config *);
    uint8_t state[8];
    char state_now[MAX_STATE_STRING_LENGTH + 1];
    char state_prev[MAX_STATE_STRING_LENGTH + 1];
} device_config;

static int output_level = 1;  // 0 = none, 1 = info, 2 = debug
static int exit_code = -1;  // -1 = run, 0 = success, 1+ = error

/**
 * Finds a device when given a vendor_id and product_id.
 *
 * name = Name of device for debug message
 * vendor_id = Vendor ID of USB device
 * product_id = Product ID of USB device
 * config = Where to store device information
 *
 * Returns 0 on success
 */
static int seek_device(const char *name, int vendor_id, int product_id, device_config *config) {
    struct libusb_device_handle *handle = NULL;

    DEBUG("Attempting to open %s (vendor 0x%04x, device 0x%04x)", name, vendor_id, product_id);
    handle = libusb_open_device_with_vid_pid(NULL, vendor_id, product_id);

    if (!handle) {
        return 1;
    }

    config->name = name;
    config->vendor_id = vendor_id;
    config->product_id = product_id;
    config->handle = handle;

    return 0;
}


/**
 * Detaches the kernel driver if it is currently attached.
 *
 * handle = USB device
 *
 * returns 0 on success, 1 on failure.
 */
static int detach_kernel_driver(libusb_device_handle *handle) {
    /* If the kernel driver is active, we need to detach it */
    if (libusb_kernel_driver_active(handle, 0)) {
        DEBUG("Kernel driver active, attempting to detach");

        if (LIBUSB_SUCCESS != libusb_detach_kernel_driver(handle, 0)) {
            return 1;
        }

        DEBUG("Kernel driver detached successfully");
    } else {
        DEBUG("Kernel driver not active");
    }

    return 0;
}


/**
 * Attempt to read the current button state.
 *
 * device = USB device
 *
 * Returns -1 on hard error, 0 on success, 1 on soft error.
 */
static int interrupt_transfer(device_config *device) {
    int ret, transferred;

    memset(device->state, 0, 8);

    /* Use endpoint 0x81 and retrieve the state */
    ret = libusb_interrupt_transfer(device->handle, LIBUSB_ENDPOINT_IN | 0x01, device->state, 8, &transferred, 200);

    if (LIBUSB_SUCCESS != ret) {
        // Soft error
        DEBUG("Error getting interrupt data - ignoring");

        return ret;
    }

    if (transferred < 8) {
        DEBUG("Transferred %d of %d bytes", transferred, 8);

        return 8 - transferred;
    }

    return 0;
}


// Dream Cheeky - USB Fidget
// Trusting other source code for this routine
// 1E = button pressed
static void convert_state_1d34_0001(device_config *device) {
    if (device->state[0] == 0x1E) {
        device->state_now[0] = '1';
    } else {
        device->state_now[0] = '0';
    }

    device->state_now[1] = '\0';
}


// Dream Cheeky - Big Red Button
// 01110 = BUTTON lid
// 01111 = button lid
// 10000 = BUTTON LID
// 10001 = button LID
static void convert_state_1d34_000d(device_config *device) {
    // bit 1: on = button not pressed
    if (device->state[0] & 0x01) {
        device->state_now[0] = '0';
    } else {
        device->state_now[0] = '1';
    }

    device->state_now[1] = ' ';

    // bit 5: on = lid open
    if (device->state[0] & 0x10) {
        device->state_now[2] = '1';
    } else {
        device->state_now[2] = '0';
    }

    device->state_now[3] = '\0';
}


static int read_state_1d34_0020(device_config *device) {
    uint8_t rep1[8] = { 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 },
        rep2[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09 };
    int ret;

    // Tell the sensors to start working (or something like that)
    ret = libusb_control_transfer(device->handle, CONTROL_REQUEST_TYPE_OUT, HID_REPORT, 0x0200, 0x00, rep1, 8, 200);

    // We expect IO errors from this call
    if (ret < 0 && ret != LIBUSB_ERROR_IO) {
        return ret;
    }

    // Read the response
    ret = interrupt_transfer(device);

    if (ret) {
        return ret;
    }

    // Report on the sensors
    ret = libusb_control_transfer(device->handle, CONTROL_REQUEST_TYPE_OUT, HID_REPORT, 0x0200, 0x00, rep2, 8, 200);

    // Again, we expect IO errors
    if (ret < 0 && ret != LIBUSB_ERROR_IO) {
        return ret;
    }

    // Read the sensors
    ret = interrupt_transfer(device);

    if (ret) {
        return ret;
    }

    // Sometimes the device doesn't send us valid data.
    // The last byte should never be 0x00.
    if (device->state[7] == 0x00) {
        return 1;  // Soft error reading state
    }

    return 0;
}


static void convert_state_1d34_0020(device_config *device) {
    // Convert into arguments
    sprintf(device->state_now, "%d %d %d", device->state[0], device->state[1], device->state[2]);
}


// EB Brands - USB ! Key
static void convert_state_1130_6626(device_config *device) {
    if (device->state[0] == 0x68) {
        device->state_now[0] = '1';
    } else {
        device->state_now[0] = '0';
    }

    device->state_now[1] = '\0';
}


/**
 * Attempt to read the current button state.
 *
 * device = USB device
 *
 * Returns -1 on hard error, 0 on success, 1 on soft error.
 */
static int read_state_generic(device_config *device) {
    uint8_t rep[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 };
    int ret;

    ret = libusb_control_transfer(device->handle, CONTROL_REQUEST_TYPE_OUT, HID_REPORT, 0x0000, 0x00, rep, 8, 200);

    if (ret < 0) {
        ERROR("Error sending initialization - libusb error %d", ret);

        return -1;
    }

    if (ret < 8) {
        ERROR("Short write - missing %d bytes", 8 - ret);

        return -1;
    }

    ret = interrupt_transfer(device);

    return ret;
}


/**
 * Repeatedly try to read the button state, pausing between reads.
 *
 * If a hard error is reported or there is success, this function exits.
 *
 * device = USB device
 * dest = where to store the resulting byte
 * interval = how long to wait between soft errors and trying again
 *
 * returns 0 on success, <0 on hard error, >0 on soft error
 */
static int repeat_read_button_state(device_config *device, int interval) {
    int result;

    while (exit_code == -1) {
        result = device->read_state(device);

        if (result == 0) {
            // DEBUG("State: %02x %02x %02x %02x  %02x %02x %02x %02x", device->state[0], device->state[1], device->state[2], device->state[3], device->state[4], device->state[5], device->state[6], device->state[7]);
            device->convert_state(device);
            // DEBUG("State converted: %s", device->state_now);
        }

        if (result <= 0) {
            return result;
        }

        usleep(interval);
    }

    return 1;
}


/**
 * Run a command.  This also adds the current and previous status
 * as command-line arguments.
 *
 * cmd = command to run (we add two more arguments)
 * now = current status
 * then = previous status
 */
void run_command(char const *cmd, char const *now, char const *prev) {
    static char *modified = NULL;
    int ret, bytes;

    if (cmd) {
        if (!modified) {
            bytes = strlen(cmd) + MAX_STATE_STRING_LENGTH * 2 + 3;
            modified = malloc(bytes);

            if (!modified) {
                ERROR("Could not allocate memory for command (%d bytes)", bytes);
                exit_code = 1;

                return;
            }
        }

        sprintf(modified, "%s %s %s", cmd, now, prev);
        DEBUG("Running command: %s", modified);
        ret = system(modified);
        DEBUG("Command returned %i", ret);
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
 * device = the device
 * interval = usleep time
 * command = command to execute on status updates
 */
static void run_detector(device_config *device, int interval, const char *command) {
    DEBUG("Polling for events");

    // Poll the device to get the status until SIGINT or hard error
    while (exit_code == -1) {
        if (repeat_read_button_state(device, interval)) {
            exit_code = 1;

            return;
        }

        if (strcmp(device->state_now, device->state_prev)) {
            INFO("State switched from '%s' to '%s'", device->state_prev, device->state_now);
            run_command(command, device->state_now, device->state_prev);
            strcpy(device->state_prev, device->state_now);
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
 * Opens the handle and configures the device
 *
 * device = where to store the configured device information
 *
 * Returns 0 on success
 */
static int scan_all_devices(device_config *device) {
    // Clear the entire structure.  This does a lot for us, such as
    // nulling strings and setting all defaults to zero.
    memset(device, 0, sizeof(device_config));

    // Set reasonable defaults
    device->read_state = read_state_generic;

    // Try to get a handle for each supported device
    if (!seek_device("Dream Cheeky - USB Fidget", 0x1d34, 0x0001, device)) {
        device->convert_state = convert_state_1d34_0001;

        return 0;
    }

    if (!seek_device("Dream Cheeky - Big Red Button", 0x1d34, 0x000d, device)) {
        device->convert_state = convert_state_1d34_000d;

        return 0;
    }

    if (!seek_device("Dream Cheeky - Stress Ball", 0x1d34, 0x0020, device)) {
        device->wValue = 0x0200;
        device->read_state = read_state_1d34_0020;
        device->convert_state = convert_state_1d34_0020;

        return 0;
    }

    if (!seek_device("EB Brands - USB ! Key", 0x1130, 0x6626, device)) {
        device->convert_state = convert_state_1130_6626;

        return 0;
    }

    return 1;
}


/**
 * Our program
 */
int main(int argc, char **argv) {
    char const *command = NULL;
    int interval = 20000;
    device_config device;

    // Handle arguments to our program
    parse_arguments(argc, argv, &interval, &command);

    // In case any arguments were invalid
    if (exit_code != -1) {
        return exit_code;
    }

    // Setup a signal handler, so we can cleanup gracefully
    //signal(SIGINT, exit_handler);

    // Initialise libusb (with the default context)
    if (LIBUSB_SUCCESS != libusb_init(NULL)) {
        ERROR("Unable to initialize libusb");

        return 1;
    }

    if (scan_all_devices(&device)) {
        ERROR("Failed opening device descriptor (you may need to be root)...");

        return 1;
    }

    // Detach the kernel driver if it is attached
    if (detach_kernel_driver(device.handle)) {
        ERROR("Can't detach kernel driver");

        return 1;
    }

    if (LIBUSB_SUCCESS != libusb_claim_interface(device.handle, 0)) {
        ERROR("Can't claim interface");

        return 1;
    }

    DEBUG("Interface claimed");

    // Run the detector - this polls the device in a loop
    run_detector(&device, interval, command);

    // We are done
    DEBUG("Closing USB");
    fflush(stdout);
    fflush(stderr);
    libusb_close(device.handle);

    return exit_code;
}
