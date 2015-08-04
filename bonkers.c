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

#define LOG(level, _fmt, ...) if (output_level > level) { fprintf(stdout, _fmt "\n", ## __VA_ARGS__); fflush(stdout); }
#define ERROR(_fmt, ...) fprintf(stderr, _fmt "\n", ## __VA_ARGS__); fflush(stderr);

#define CONTROL_REQUEST_TYPE_OUT LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE

// Actions
#define HID_REPORT 0x09

// Max number of characters in a converted state string (not including NULL)
#define MAX_STATE_STRING_LENGTH 11

// Return codes
typedef enum {
    BONKERS_RUN = -1,
    BONKERS_SUCCESS,
    BONKERS_ERROR,
    BONKERS_WARN
} bonkers_result;


/* This structure is passed around a lot.  It contains the information
 * necessary to contact and communicate with a device.
 */
typedef struct device_config {
    const char *name;
    int vendor_id;
    int product_id;
    struct libusb_device_handle *handle;
    uint16_t wValue;
    bonkers_result (*read_state)(struct device_config *);
    void (*convert_state)(struct device_config *);
    uint8_t state[8];
    char state_now[MAX_STATE_STRING_LENGTH + 1];
    char state_prev[MAX_STATE_STRING_LENGTH + 1];
} device_config;


static int output_level = 1;  // 0 = none, 1 = info, 2+ = debug
static bonkers_result exit_code = -1;  // -1 = run, 0 = success, 1+ = error


/**
 * Finds a device when given a vendor_id and product_id.
 *
 * name = Name of device for debug message
 * vendor_id = Vendor ID of USB device
 * product_id = Product ID of USB device
 * config = Where to store device information
 */
static bonkers_result seek_device(const char *name, int vendor_id, int product_id, device_config *config) {
    struct libusb_device_handle *handle = NULL;

    LOG(1, "Attempting to open %s (vendor 0x%04x, device 0x%04x)", name, vendor_id, product_id);
    handle = libusb_open_device_with_vid_pid(NULL, vendor_id, product_id);

    if (!handle) {
        return BONKERS_ERROR;
    }

    config->name = name;
    config->vendor_id = vendor_id;
    config->product_id = product_id;
    config->handle = handle;

    return BONKERS_SUCCESS;
}


/**
 * Detaches the kernel driver if it is currently attached.
 *
 * handle = USB device
 */
static bonkers_result detach_kernel_driver(libusb_device_handle *handle) {
    /* If the kernel driver is active, we need to detach it */
    if (libusb_kernel_driver_active(handle, 0)) {
        LOG(1, "Kernel driver active, attempting to detach");

        if (LIBUSB_SUCCESS != libusb_detach_kernel_driver(handle, 0)) {
            return BONKERS_ERROR;
        }

        LOG(1, "Kernel driver detached successfully");
    } else {
        LOG(1, "Kernel driver not active");
    }

    return BONKERS_SUCCESS;
}


/**
 * Attempt to read the current button state.
 *
 * device = USB device
 * timeout = timeout in MS, 0 to wait forever
 *
 * Returns libusb status code.
 */
static bonkers_result interrupt_transfer(device_config *device, int timeout) {
    int ret, transferred;

    memset(device->state, 0, 8);

    /* Use endpoint 0x81 and retrieve the state */
    ret = libusb_interrupt_transfer(device->handle, LIBUSB_ENDPOINT_IN | 0x01, device->state, 8, &transferred, timeout);

    if (LIBUSB_SUCCESS != ret) {
        // Soft error
        LOG(1, "Error getting interrupt data: %d", ret);

        return ret;
    }

    if (transferred < 8) {
        LOG(1, "Transferred %d of %d bytes", transferred, 8);

        return ret;
    }

    return 0;
}


/* Attempt a control_transfer
 *
 * device = The device
 * timeout = The timeout in ms
 *
 * Returns libusb status code.
 */
static int control_transfer_out_report(device_config *device, uint16_t wValue, uint16_t wIndex, unsigned char *data, uint16_t data_len, unsigned int timeout) {
    int ret;

    ret = libusb_control_transfer(device->handle, CONTROL_REQUEST_TYPE_OUT, HID_REPORT, wValue, wIndex, data, data_len, timeout);

    if (ret < 0) {
        LOG(1, "Error sending report - libusb error %d", ret);

        return ret;
    }

    if (ret < data_len) {
        ERROR("Short write - sent %d of %d bytes", ret, data_len);

        return ret;
    }

    return ret;
}


/* Dream Cheeky - USB Fidget
 * Device ID:  1d34:0001 (Soccer - Untested)
 * Device ID:  1d34:0001 (Basketball)
 * Device ID:  1d34:0003 (Golf)
 *
 * Button not pressed
 *     1f 00 00 00  00 00 00 03
 * Button pressed
 *     1e 00 00 00  00 00 00 03
 */
static void convert_state_1d34_fidget(device_config *device) {
    if (device->state[0] == 0x1E) {
        device->state_now[0] = '1';
    } else {
        device->state_now[0] = '0';
    }

    device->state_now[1] = '\0';
}


static bonkers_result read_state_1d34_fidget(device_config *device) {
    uint8_t rep[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 };
    int ret;

    ret = control_transfer_out_report(device, 0x0000, 0x0000, rep, 8, 200);

    if (ret != 8) {
        return BONKERS_ERROR;
    }

    ret = interrupt_transfer(device, 200);

    // Ignore timeout errors
    if (ret == LIBUSB_ERROR_TIMEOUT) {
        return BONKERS_WARN;
    }

    if (ret) {
        return BONKERS_ERROR;
    }

    return BONKERS_SUCCESS;
}


/* Dream Cheeky - Big Red Button
 * Device ID:  1d34:0004
 *
 * Lid closed, button pressed:  (first byte:  0001 0100)
 *     14 00 00 00  00 00 00 03
 * Lid closed, button not pressed:  (first byte:  0001 0101)
 *     15 00 00 00  00 00 00 03
 * Lid open, button pressed:  (first byte:  0001 0110)
 *     16 00 00 00  00 00 00 03
 * Lid open, button not pressed: (first byte:  0001 0111)
 *     17 00 00 00  00 00 00 03
 */
static void convert_state_1d34_000d(device_config *device) {
    // bit 1: on = button not pressed
    if (device->state[0] & 0x01) {
        device->state_now[0] = '0';
    } else {
        device->state_now[0] = '1';
    }

    device->state_now[1] = ' ';

    // bit 2: on = lid open
    if (device->state[0] & 0x02) {
        device->state_now[2] = '1';
    } else {
        device->state_now[2] = '0';
    }

    device->state_now[3] = '\0';
}


static bonkers_result read_state_1d34_000d(device_config *device) {
    uint8_t rep[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 };
    int ret;

    ret = control_transfer_out_report(device, 0x0000, 0x0000, rep, 8, 200);

    if (ret != 8) {
        return BONKERS_ERROR;
    }

    ret = interrupt_transfer(device, 200);

    // Ignore occasional timeout errors
    if (ret == LIBUSB_ERROR_TIMEOUT) {
        return BONKERS_WARN;
    }

    if (ret) {
        return BONKERS_ERROR;
    }

    return BONKERS_SUCCESS;
}


/* Dream Cheeky - Stress Ball
 * Device ID:  1d34:0020
 *
 * First three bytes are the sensors.  Here is an example for my device
 * at rest:
 *     6c 8c b1 fb  00 00 00 03
 *
 * The first number (6c) is the squeeze sensor.  When squeezed, mine will
 * drop to about 08.
 *
 * The second number (8c) measures twist.  When twisted left, mine drops to
 * 08 and twisting right this goes up to ff.
 *
 * The third number (b1) measures the pull/push.  When pushed, mine will go
 * down to 08 and pulling will increase it to ff.
 */
static void convert_state_1d34_0020(device_config *device) {
    // Convert into arguments
    sprintf(device->state_now, "%d %d %d", device->state[0], device->state[1], device->state[2]);
}


static int read_state_1d34_0020(device_config *device) {
    uint8_t rep1[8] = { 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 },
        rep2[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09 };
    int ret;

    // Tell the sensors to start working (or something like that)
    ret = control_transfer_out_report(device, 0x0200, 0x0000, rep1, 8, 200);

    // We expect IO errors from this call
    if (ret != 8 && ret != LIBUSB_ERROR_IO) {
        return BONKERS_ERROR;
    }

    // Read the response
    ret = interrupt_transfer(device, 200);

    if (ret) {
        return BONKERS_ERROR;
    }

    // Report on the sensors
    ret = control_transfer_out_report(device, 0x0200, 0x0000, rep2, 8, 200);

    // Again, we expect IO errors
    if (ret != 8 && ret != LIBUSB_ERROR_IO) {
        return BONKERS_ERROR;
    }

    // Read the sensors
    ret = interrupt_transfer(device, 200);

    if (ret) {
        return BONKERS_ERROR;
    }

    // Sometimes the device doesn't send us valid data.
    // The last byte should never be 0x00.
    if (device->state[7] == 0x00) {
        return BONKERS_WARN;  // Soft error reading state
    }

    return BONKERS_SUCCESS;
}


/* EB Brands - USB ! Key
 * Device ID:  1130:6626
 *
 * This will time out the report when no events happen, so the code just
 * waits for an event to occur.
 *
 * When pressed the button will provide
 *     68 00 26 00  00 00 00 00
 * and then immediately provide
 *     00 00 00 00  00 00 00 00
 *
 * Holding the button down doesn't do anything special and it just returns
 * the two codes immediately.  You can not detect a held button.
 */
static void convert_state_1130_6626(device_config *device) {
    if (device->state[0] == 0x68) {
        device->state_now[0] = '1';
    } else {
        device->state_now[0] = '0';
    }

    device->state_now[1] = '\0';
}


static bonkers_result read_state_1130_6626(device_config *device) {
    int ret;

    ret = interrupt_transfer(device, 0);

    if (ret) {
        return BONKERS_ERROR;
    }

    return BONKERS_SUCCESS;
}


/**
 * Repeatedly try to read the button state, pausing between reads.
 *
 * If a hard error is reported or there is success, this function exits.
 *
 * device = USB device
 * dest = where to store the resulting byte
 * interval = how long to wait between soft errors and trying again
 */
static int repeat_read_button_state(device_config *device, int interval) {
    int result;

    while (exit_code == -1) {
        result = device->read_state(device);

        if (result == BONKERS_SUCCESS) {
            LOG(2, "State: %02x %02x %02x %02x  %02x %02x %02x %02x", device->state[0], device->state[1], device->state[2], device->state[3], device->state[4], device->state[5], device->state[6], device->state[7]);
            device->convert_state(device);
            LOG(2, "State converted: %s", device->state_now);
        }

        if (result == BONKERS_SUCCESS || result == BONKERS_ERROR) {
            return result;
        }

        usleep(interval);
    }

    return BONKERS_WARN;
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
        LOG(1, "Running command: %s", modified);
        ret = system(modified);
        LOG(1, "Command returned %i", ret);
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
    "  -v                Turn on verbose output.  With -vv, way more is printed.\n",
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
    LOG(1, "Detecting events");

    // Poll the device to get the status until SIGINT or hard error
    while (exit_code == BONKERS_RUN) {
        if (repeat_read_button_state(device, interval)) {
            exit_code = BONKERS_ERROR;
        }

        if (exit_code == BONKERS_RUN) {
            if (strcmp(device->state_now, device->state_prev)) {
                LOG(0, "State switched from '%s' to '%s'", device->state_prev, device->state_now);
                run_command(command, device->state_now, device->state_prev);
                strcpy(device->state_prev, device->state_now);
            }

            usleep(interval);
        }
    }

    LOG(1, "Exit code was changed: %d", exit_code);
}


/**
 * Use getopt to parse the command-line arguments.
 *
 * argc, argv = same as main
 * interval = where to set an interval if one is passed
 * command = where to assign the pointer for the command to execute
 */
void parse_arguments(int argc, char **argv, int *interval, const char **command) {
    int c;  // This typically stores a character but EOF is an integer

    while ((c = getopt(argc, argv, "c:hp:qv")) != EOF) {
        switch (c) {
            case 'c':
                *command = optarg;
                break;

            case 'h':
                usage(argv[0]);
                exit_code = BONKERS_SUCCESS;

                return;

            case 'p':
                *interval = atoi(optarg);

                if (*interval <= 0) {
                    exit_code = BONKERS_ERROR;

                    return;
                }

                break;

            case 'q':
                output_level = 0;
                break;

            case 'v':
                output_level ++;
                break;
        }
    }
}


/**
 * Opens the handle and configures the device
 *
 * device = where to store the configured device information
 */
static bonkers_result scan_all_devices(device_config *device) {
    // Clear the entire structure.  This does a lot for us, such as
    // nulling strings and setting all defaults to zero.
    memset(device, 0, sizeof(device_config));

    // Try to get a handle for each supported device
    if (!seek_device("Dream Cheeky - USB Fidget (Soccer)", 0x1d34, 0x0001, device)) {
        device->read_state = read_state_1d34_fidget;
        device->convert_state = convert_state_1d34_fidget;

        return BONKERS_SUCCESS;
    }

    if (!seek_device("Dream Cheeky - USB Fidget (Basketball)", 0x1d34, 0x0002, device)) {
        device->read_state = read_state_1d34_fidget;
        device->convert_state = convert_state_1d34_fidget;

        return BONKERS_SUCCESS;
    }

    if (!seek_device("Dream Cheeky - USB Fidget (Golf)", 0x1d34, 0x0003, device)) {
        device->read_state = read_state_1d34_fidget;
        device->convert_state = convert_state_1d34_fidget;

        return BONKERS_SUCCESS;
    }

    if (!seek_device("Dream Cheeky - Big Red Button", 0x1d34, 0x000d, device)) {
        device->read_state = read_state_1d34_000d;
        device->convert_state = convert_state_1d34_000d;

        return BONKERS_SUCCESS;
    }

    if (!seek_device("Dream Cheeky - Stress Ball", 0x1d34, 0x0020, device)) {
        device->wValue = 0x0200;
        device->read_state = read_state_1d34_0020;
        device->convert_state = convert_state_1d34_0020;

        return BONKERS_SUCCESS;
    }

    if (!seek_device("EB Brands - USB ! Key", 0x1130, 0x6626, device)) {
        device->read_state = read_state_1130_6626;
        device->convert_state = convert_state_1130_6626;

        return BONKERS_SUCCESS;
    }

    return BONKERS_ERROR;
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
    if (exit_code != BONKERS_RUN) {
        return exit_code;
    }

    // Setup a signal handler, so we can cleanup gracefully
    signal(SIGINT, exit_handler);

    // Initialise libusb (with the default context)
    if (LIBUSB_SUCCESS != libusb_init(NULL)) {
        ERROR("Unable to initialize libusb");

        return BONKERS_ERROR;
    }

    if (scan_all_devices(&device)) {
        ERROR("Failed opening device descriptor (you may need to be root)...");

        return BONKERS_ERROR;
    }

    // Detach the kernel driver if it is attached
    if (detach_kernel_driver(device.handle)) {
        ERROR("Can't detach kernel driver");

        return BONKERS_ERROR;
    }

    if (LIBUSB_SUCCESS != libusb_claim_interface(device.handle, 0)) {
        ERROR("Can't claim interface");

        return BONKERS_ERROR;
    }

    LOG(1, "Interface claimed");

    // Run the detector - this polls the device in a loop
    run_detector(&device, interval, command);

    // We are done
    LOG(1, "Closing USB");
    fflush(stdout);
    fflush(stderr);
    libusb_close(device.handle);

    return exit_code;
}
