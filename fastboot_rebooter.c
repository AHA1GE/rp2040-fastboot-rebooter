#include "pico/stdlib.h"
#include "tusb.h"

#define REBOOT_CMD "reboot\x00"
#define REBOOT_CMD_LEN (sizeof(REBOOT_CMD) - 1)

// Transfer structure
tuh_xfer_t xfer;

void send_fastboot_reboot(uint8_t dev_addr, uint8_t ep_addr);
void transfer_complete_cb(tuh_xfer_t *xfer);
void descriptor_complete_cb(tuh_xfer_t *xfer);

// Callback when a device is mounted (connected)
void tuh_mount_cb(uint8_t dev_addr)
{
    printf("Device attached, address = %d\n", dev_addr);
    // get device descriptor
    static uint8_t desc[512]; // Big enough for device descriptor
    // store result of tuh_descriptor_get_configuration to check if it is successful
    bool result = tuh_descriptor_get_configuration(dev_addr, 0, desc, sizeof(desc), descriptor_complete_cb, 0);
    if (!result)
    {
        printf("Failed to retrieve configuration descriptor\n");
    }
}

// Callback when a device mount is failed
void tuh_mount_failed_cb(uint8_t dev_addr)
{
    printf("Device mount failed, address = %d\n", dev_addr);
}

// Callback when a device is unmounted (disconnected)
void tuh_umount_cb(uint8_t dev_addr)
{
    printf("Device removed, address = %d\n", dev_addr);
}

// Callback when a descriptor is retrieved
void descriptor_complete_cb(tuh_xfer_t *xfer)
{
    // Store the descriptor data in a local buffer
    uint8_t *desc = (uint8_t *)xfer->buffer;

    // First, log the descriptor data for debugging
    printf("Descriptor data:\n");
    for (int i = 0; i < xfer->actual_len; i++)
    {
        printf("%02x ", desc[i]);
    }
    printf("\n");

    // Second, find the bulk OUT endpoint
    uint8_t dev_addr = xfer->daddr;
    uint8_t ep_addr = 0; // Initialize to 0, meaning not found

    // Iterate through the descriptor to find the bulk OUT endpoint
    while (desc < xfer->buffer + xfer->actual_len)
    {
        if (desc[1] == TUSB_DESC_INTERFACE)
        {
            // Found an interface descriptor
            printf("Interface found: class = %d, subclass = %d, protocol = %d\n", desc[5], desc[6], desc[7]);
        }
        else if (desc[1] == TUSB_DESC_ENDPOINT)
        {
            // Found an endpoint descriptor
            uint8_t endpoint_address = desc[2];
            uint8_t attributes = desc[3];

            // If this is a BULK OUT endpoint, set ep_addr and break
            if ((endpoint_address & TUSB_DIR_IN_MASK) == 0 && (attributes & TUSB_XFER_BULK) != 0)
            {
                ep_addr = endpoint_address;
                printf("Bulk OUT endpoint found at address 0x%02x\n", ep_addr);
                // Attempt to open the endpoint
                if (tuh_edpt_open(dev_addr, desc))
                {
                    printf("Bulk OUT endpoint opened at address 0x%02x\n", ep_addr);
                }
                else
                {
                    ep_addr = 0; // Reset to 0 if the endpoint cannot be opened
                    printf("Failed to open Bulk OUT endpoint at address 0x%02x, break!\n", ep_addr);
                    return; // Do not proceed if the endpoint cannot be opened
                }
                break;
            }
        }

        // Move to the next descriptor in the configuration descriptor
        desc += desc[0];
    }

    if (ep_addr != 0)
    {
        printf("Sending Fastboot reboot command to device %d's endpoint %d\n", dev_addr, ep_addr);
        // Send the fastboot reboot command
        send_fastboot_reboot(dev_addr, ep_addr);
    }
    else
    {
        printf("Bulk OUT endpoint not found.\n");
    }
}

// Send the fastboot reboot command to the device
void send_fastboot_reboot(uint8_t dev_addr, uint8_t ep_addr)
{
    tuh_xfer_t xfer;
    uint8_t cmd[] = REBOOT_CMD;

    // Initialize the transfer structure
    xfer.daddr = dev_addr;                   // Device address
    xfer.ep_addr = ep_addr;                  // Endpoint address
    xfer.result = 0;                         // Initialize result, may be updated by the transfer
    xfer.actual_len = 0;                     // Actual length of data transferred, initialized to 0
    xfer.buffer = cmd;                       // Pointer to the data buffer
    xfer.buflen = REBOOT_CMD_LEN;            // Length of the data buffer
    xfer.complete_cb = transfer_complete_cb; // Callback function
    xfer.user_data = 0;                      // User data, initialized to 0

    // Send the command using tuh_edpt_xfer
    if (tuh_edpt_xfer(&xfer))
    {
        printf("Fastboot reboot command sent to device %d's endpoint %d\n", dev_addr, ep_addr);
    }
    else
    {
        printf("Failed to send Fastboot reboot command to device %d's endpoint %d. Data:\n", dev_addr, ep_addr);
        // Print the structure for debugging
        printf("daddr: %d, ep_addr: %d, result: %d, actual_len: %d, buffer: %s, buflen: %d\n", xfer.daddr, xfer.ep_addr, xfer.result, xfer.actual_len, (char *)xfer.buffer, xfer.buflen);
    }
}

// Callback when the command is sent
void transfer_complete_cb(tuh_xfer_t *xfer)
{
    printf("Fastboot reboot completed. Result: %d, actual_len: %d\n", xfer->result, xfer->actual_len);
    if (xfer->result != XFER_RESULT_SUCCESS)
    {
        printf("Transfer failed with error code %d\n", xfer->result);
    }
}

int main()
{
    // Initialize the board and TinyUSB
    stdio_init_all();
    if (!tuh_init(TUH_OPT_RHPORT))
    {
        printf("Failed to initialize TinyUSB host stack\n");
        return -1;
    }
    printf("init DONE! Waiting for debug......\n");
    sleep_ms(3000); // Sleep for 3 seconds for UART initialization
    printf("READY!\n");
    // Main loop
    while (1)
    {
        // TinyUSB host task must be called regularly
        tuh_task();
        // Add a delay to reduce CPU usage
        sleep_ms(100);
    }

    return 0;
}
