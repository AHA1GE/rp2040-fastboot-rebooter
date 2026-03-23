#include "pico/stdlib.h"
#include "tusb.h"
#include "fastboot_cmds.h"
#include <string.h>

#define MAX_CMDS    32
#define MAX_CMD_LEN 256

// Parsed commands loaded from the embedded FASTBOOTCMDS.txt content
static char g_cmds[MAX_CMDS][MAX_CMD_LEN];
static int  g_cmd_count = 0;

// State for sequential command dispatch
static uint8_t g_dev_addr  = 0;
static uint8_t g_ep_addr   = 0;
static int     g_cmd_index = 0;

// Forward declarations
void send_fastboot_cmd(uint8_t dev_addr, uint8_t ep_addr, const char *cmd, uint32_t cmd_len);
void transfer_complete_cb(tuh_xfer_t *xfer);
void descriptor_complete_cb(tuh_xfer_t *xfer);

// Parse the embedded FASTBOOT_CMDS string (newline-delimited) into g_cmds[]
static void parse_fastboot_cmds(void)
{
    const char *src = FASTBOOT_CMDS;
    int cmd_idx  = 0;
    int char_idx = 0;

    while (*src && cmd_idx < MAX_CMDS)
    {
        if (*src == '\n')
        {
            if (char_idx > 0)
            {
                g_cmds[cmd_idx][char_idx] = '\0';
                cmd_idx++;
                char_idx = 0;
            }
        }
        else if (*src != '\r' && char_idx < MAX_CMD_LEN - 1)
        {
            g_cmds[cmd_idx][char_idx++] = *src;
        }
        src++;
    }
    // Handle last line without a trailing newline
    if (char_idx > 0 && cmd_idx < MAX_CMDS)
    {
        g_cmds[cmd_idx][char_idx] = '\0';
        cmd_idx++;
    }
    g_cmd_count = cmd_idx;
    printf("Loaded %d fastboot command(s) from FASTBOOTCMDS.txt\n", g_cmd_count);
}

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
        if (g_cmd_count == 0)
        {
            printf("No fastboot commands to send.\n");
            return;
        }
        printf("Sending fastboot commands to device %d's endpoint 0x%02x\n", dev_addr, ep_addr);
        g_dev_addr  = dev_addr;
        g_ep_addr   = ep_addr;
        g_cmd_index = 0;
        send_fastboot_cmd(dev_addr, ep_addr, g_cmds[0], strlen(g_cmds[0]));
    }
    else
    {
        printf("Bulk OUT endpoint not found.\n");
    }
}

// Send a single fastboot command to the device
void send_fastboot_cmd(uint8_t dev_addr, uint8_t ep_addr, const char *cmd, uint32_t cmd_len)
{
    // Static buffer — must remain valid until transfer_complete_cb fires
    static uint8_t cmd_buf[MAX_CMD_LEN + 1];

    if (cmd_len > MAX_CMD_LEN)
    {
        printf("Command too long (%lu bytes), skipping: %s\n", (unsigned long)cmd_len, cmd);
        return;
    }

    memcpy(cmd_buf, cmd, cmd_len);
    cmd_buf[cmd_len] = '\0'; // Append null byte to match fastboot protocol behaviour

    tuh_xfer_t xfer;
    xfer.daddr       = dev_addr;
    xfer.ep_addr     = ep_addr;
    xfer.result      = 0;
    xfer.actual_len  = 0;
    xfer.buffer      = cmd_buf;
    xfer.buflen      = cmd_len + 1; // Include the null byte
    xfer.complete_cb = transfer_complete_cb;
    xfer.user_data   = 0;

    printf("Sending fastboot command [%d/%d]: %s\n", g_cmd_index + 1, g_cmd_count, cmd);
    if (tuh_edpt_xfer(&xfer))
    {
        printf("Fastboot command sent to device %d's endpoint 0x%02x\n", dev_addr, ep_addr);
    }
    else
    {
        printf("Failed to send fastboot command to device %d's endpoint 0x%02x. Data:\n", dev_addr, ep_addr);
        printf("daddr: %d, ep_addr: %d, result: %d, actual_len: %d, buffer: %s, buflen: %lu\n",
               xfer.daddr, xfer.ep_addr, xfer.result, xfer.actual_len,
               (char *)xfer.buffer, (unsigned long)xfer.buflen);
    }
}

// Callback when a command transfer is complete — sends the next command after a 2-second delay
void transfer_complete_cb(tuh_xfer_t *xfer)
{
    printf("Fastboot command completed. Result: %d, actual_len: %d\n", xfer->result, xfer->actual_len);
    if (xfer->result != XFER_RESULT_SUCCESS)
    {
        printf("Transfer failed with error code %d\n", xfer->result);
        return;
    }

    g_cmd_index++;
    if (g_cmd_index < g_cmd_count)
    {
        printf("Waiting 2 seconds before sending next command...\n");
        sleep_ms(2000);
        send_fastboot_cmd(g_dev_addr, g_ep_addr, g_cmds[g_cmd_index], strlen(g_cmds[g_cmd_index]));
    }
    else
    {
        printf("All fastboot commands executed.\n");
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

    // Load fastboot commands from the embedded FASTBOOTCMDS.txt content
    parse_fastboot_cmds();

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

