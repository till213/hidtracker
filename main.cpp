#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linuxtrack.h"

#define FOOHID_CREATE 0
#define FOOHID_DESTROY 1
#define FOOHID_SEND 2
#define FOOHID_LIST 3

static const char *FoohidService = "it_unbit_foohid";
static const char *FoohidDevice = "Virtual GamePad FooBar";

// A somewhat complicated way to describe a mouse
unsigned char report_descriptor[] =
{
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                    // USAGE (Mouse)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,                    //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x75, 0x05,                    //     REPORT_SIZE (5)
    0x81, 0x03,                    //     INPUT (Cnst,Var,Abs)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)
    0xc0,                          //   END_COLLECTION
    0xc0                           // END_COLLECTION
};

struct mouse_report_t
{
    uint8_t buttons;
    int8_t x;
    int8_t y;
};

// Initial code inspired by: https://github.com/unbit/foohid-py/blob/master/foohid.c

// linuxTracker results
static float heading, pitch, roll, x, y, z;
static unsigned int counter;

bool intialise_tracking(void)
{
    linuxtrack_state_type state;
    // Initialize the tracking using Default profile
    state = linuxtrack_init(nullptr);
    if (state < LINUXTRACK_OK){
        printf("%s\n", linuxtrack_explain(state));
        return false;
    }
    int timeout = 0;
    // Wait up to 20 seconds for the tracker initialization
    while (timeout < 200){
        state = linuxtrack_get_tracking_state();
        printf("Status: %s\n", linuxtrack_explain(state));
        if((state == RUNNING) || (state == PAUSED)){
            return true;
        }
        usleep(100000);
        ++timeout;
    }
    printf("Linuxtrack doesn't work right!\n");
    printf("Make sure it is installed and configured correctly.\n");
    return false;
}

static int foohid_connect(io_connect_t *conn)
{
    io_iterator_t iterator;
    io_service_t service;
    kern_return_t ret = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(FoohidService), &iterator);
    if (ret != KERN_SUCCESS) {
        return -1;
    }

    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        ret = IOServiceOpen(service, mach_task_self(), 0, conn);
        if (ret == KERN_SUCCESS) {
            IOObjectRelease(iterator);
            return 0;
        }
    }

    IOObjectRelease(iterator);
    return 0;
}

static void foohid_close(io_connect_t conn)
{
    IOServiceClose(conn);
}

static int foohid_create(const char *name, int name_len, uint64_t descriptor, int descriptor_len)
{
    char *deviceName;

    if (name_len == 0 || descriptor_len == 0) {
        printf("Invalid name or descriptor len values\n");
        return -1;
    }

    io_connect_t conn;
    if (foohid_connect(&conn)) {
        printf("Unable to open %s service\n", FoohidService);
        return -2;
    }

    uint32_t output_count = 1;
    uint64_t output = 0;

    uint64_t input[4];
    // The name needs to be allocated on the heap, in order to be accessible from the kernel driver
    deviceName = strdup(name);
    input[0] = (uint64_t) deviceName;
    input[1] = (uint64_t) name_len;
    input[2] = (uint64_t) descriptor;
    input[3] = (uint64_t) descriptor_len;

    kern_return_t ret = IOConnectCallScalarMethod(conn, FOOHID_CREATE, input, 4, &output, &output_count);
    free(deviceName);
    foohid_close(conn);

    if (ret != KERN_SUCCESS || output != 0) {
        printf("unable to create device, name: %s\n", name);
        return -3;
    }

    return 0;
}

static int foohid_send(const char *name, int name_len, uint64_t descriptor, int descriptor_len)
{
    char *deviceName;
    if (name_len == 0 || descriptor_len == 0) {
        printf("Invalid name or descriptor len values\n");
        return -1;
    }

    io_connect_t conn;
    if (foohid_connect(&conn)) {
        printf("Unable to open %s service\n", FoohidService);
        return -2;
    }

    uint32_t output_count = 1;
    uint64_t output = 0;

    uint64_t input[4];
    // the name needs to be allocated on the heap, in order to be accessible from the kernel driver
    deviceName = strdup(name);
    input[0] = (uint64_t) deviceName;
    input[1] = (uint64_t) name_len;
    input[2] = (uint64_t) descriptor;
    input[3] = (uint64_t) descriptor_len;

    kern_return_t ret = IOConnectCallScalarMethod(conn, FOOHID_SEND, input, 4, &output, &output_count);
    free(deviceName);
    foohid_close(conn);

    if (ret != KERN_SUCCESS || output != 0) {
        printf("unable to send hid message\n");
        return -3;
    }

    return 0;
}

static int foohid_destroy(const char *name, int name_len)
{
    char *deviceName;

    if (name_len == 0) {
        printf("Invalid name value\n");
        return -1;
    }

    io_connect_t conn;
    if (foohid_connect(&conn)) {
        printf("Unable to open %s service\n", FoohidService);
        return -2;
    }

    uint32_t output_count = 1;
    uint64_t output = 0;

    uint64_t input[2];
    // the name needs to be allocated on the heap, in order to be accessible from the kernel driver
    deviceName = strdup(name);
    input[0] = (uint64_t) deviceName;
    input[1] = (uint64_t) name_len;

    kern_return_t ret = IOConnectCallScalarMethod(conn, FOOHID_DESTROY, input, 2, &output, &output_count);
    free(deviceName);
    foohid_close(conn);

    if (ret != KERN_SUCCESS || output != 0) {
        printf("unable to destroy device, name: %s\n", name);
        return -3;
    }

    return 0;
}

int main(int argc, const char **argv)
{
    (void)(argc);
    (void)(argv);

    int ret;

    // Pre-emptively destory any previous device that might still be around
    foohid_destroy(FoohidDevice, strlen(FoohidDevice));

    // Create the virtual HID device
    ret = foohid_create(FoohidDevice, strlen(FoohidDevice), (uint64_t)report_descriptor, sizeof(report_descriptor));
    if (ret != 0) {
        printf("Unable to create device.\n");
        exit(1);
    }

    // Now initialise linuxTracker
//    bool trackingInitialised = intialise_tracking();
//    if (!trackingInitialised) {
//        printf("Initialisation of tracking failed: tracker not running or not found.\n");

//        foohid_destroy(FoohidDevice, strlen(FoohidDevice));

//        exit(1);
//    }

    struct mouse_report_t mouse;

    // Do some silly stuff
    for (int i = 0; i < 10000; ++i) {
        if (linuxtrack_get_pose(&heading, &pitch, &roll, &x, &y, &z, &counter) > 0) {
          printf("heading:%f  pitch:%f  roll:%f\n  x:%f  y:%f  z:%f\n", heading, pitch, roll, x, y, z);
        }
        mouse.buttons = 0;
        mouse.x = rand();
        mouse.y = rand();

        // ignore return value, just for testing
        ret = foohid_send(FoohidDevice, strlen(FoohidDevice), (uint64_t)&mouse, sizeof(mouse_report_t));
    }

    // Finally destroy ("disconnect") the virtual HID device again
    ret = foohid_destroy(FoohidDevice, strlen(FoohidDevice));
    if (ret != 0) {
        printf("Unable to destroy device before exit.\n");
        exit(1);
    }

    return 0;
}
