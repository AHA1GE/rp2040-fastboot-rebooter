#include "pico/stdlib.h"
#include "tusb.h"
// usbh_pvt.h is a TinyUSB-internal header that exposes usbh_class_driver_t,
// usbh_driver_set_config_complete(), usbh_edpt_claim(), usbh_edpt_release(),
// and usbh_edpt_xfer().  These are required to implement a custom app-level
// class driver via usbh_app_driver_get_cb().  If TinyUSB changes its internal
// API in a future SDK release this include may need to be revisited.
#include "host/usbh_pvt.h"
#include "fastboot_cmds.h"
#include <string.h>

#define MAX_CMDS    32
#define MAX_CMD_LEN 256

// Delay between consecutive fastboot commands (milliseconds).
// Edit this value before building to change the inter-command pause.
#define CMD_DELAY_MS 2000

// Fastboot USB interface identification (Vendor-specific class)
#define FASTBOOT_CLASS    0xFF
#define FASTBOOT_SUBCLASS 0x42
#define FASTBOOT_PROTOCOL 0x03

// Parsed commands loaded from the embedded FASTBOOTCMDS.txt content
static char g_cmds[MAX_CMDS][MAX_CMD_LEN];
static int  g_cmd_count = 0;

// State for sequential command dispatch
static uint8_t g_dev_addr  = 0;
static uint8_t g_ep_out    = 0; // bulk OUT endpoint address
static int     g_cmd_index = 0;

// Static transfer buffer — must remain valid until the transfer completes.
// Sized MAX_CMD_LEN + 1 to safely hold a MAX_CMD_LEN command plus the
// null terminator appended for the fastboot wire protocol.
static uint8_t g_cmd_buf[MAX_CMD_LEN + 1];

// Non-blocking inter-command delay state.
// When g_cmd_pending is true the main loop waits until g_next_cmd_time
// before dispatching g_cmds[g_cmd_index].
static volatile bool            g_cmd_pending   = false;
static volatile absolute_time_t g_next_cmd_time;

// Forward declarations for the Fastboot class driver callbacks
static bool fastboot_driver_init(void);
static bool fastboot_driver_deinit(void);
static bool fastboot_driver_open(uint8_t rhport, uint8_t dev_addr,
                                 tusb_desc_interface_t const *itf_desc,
                                 uint16_t max_len);
static bool fastboot_driver_set_config(uint8_t dev_addr, uint8_t itf_num);
static bool fastboot_driver_xfer_cb(uint8_t dev_addr, uint8_t ep_addr,
                                    xfer_result_t result,
                                    uint32_t xferred_bytes);
static void fastboot_driver_close(uint8_t dev_addr);
static void send_next_cmd(void);

//--------------------------------------------------------------------+
// TinyUSB application class driver registration
//
// TinyUSB's built-in vendor class driver (vendor_host.c) in the
// pico-sdk 2.0.0 bundle implements the old API and does not provide
// the cush_open / cush_set_config / cush_deinit symbols required by
// the current usbh.c class-driver table.  We therefore register our
// own lightweight driver through the weak usbh_app_driver_get_cb hook
// instead of enabling CFG_TUH_VENDOR.
//
// Without a class driver the ep2drv[] table entry for the bulk OUT
// endpoint remains uninitialised.  When the transfer completes,
// usbh.c looks up ep2drv[], finds no driver, and hits
//   TU_ASSERT(false,)   <- the crash seen in the issue log.
// Registering via usbh_app_driver_get_cb causes tu_edpt_bind_driver()
// to populate ep2drv[] correctly before our open() is called, so the
// dispatch path in tuh_task() works without error.
//--------------------------------------------------------------------+
static usbh_class_driver_t const fastboot_class_driver = {
    .name       = "FASTBOOT",
    .init       = fastboot_driver_init,
    .deinit     = fastboot_driver_deinit,
    .open       = fastboot_driver_open,
    .set_config = fastboot_driver_set_config,
    .xfer_cb    = fastboot_driver_xfer_cb,
    .close      = fastboot_driver_close,
};

// usbh_app_driver_get_cb — weak symbol defined by TinyUSB.  Overriding it
// here registers our custom Fastboot class driver with the host stack at
// initialisation time.
usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &fastboot_class_driver;
}

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

//--------------------------------------------------------------------+
// Fastboot class driver implementation
//--------------------------------------------------------------------+

static bool fastboot_driver_init(void)
{
    g_dev_addr    = 0;
    g_ep_out      = 0;
    g_cmd_index   = 0;
    g_cmd_pending = false;
    return true;
}

static bool fastboot_driver_deinit(void)
{
    return true;
}

// Called by usbh during enumeration for each interface descriptor.
// Returns true if this driver claims the interface.
static bool fastboot_driver_open(uint8_t rhport, uint8_t dev_addr,
                                 tusb_desc_interface_t const *itf_desc,
                                 uint16_t max_len)
{
    (void) rhport;

    // Only accept the Fastboot vendor-specific interface.
    if (itf_desc->bInterfaceClass    != FASTBOOT_CLASS    ||
        itf_desc->bInterfaceSubClass != FASTBOOT_SUBCLASS ||
        itf_desc->bInterfaceProtocol != FASTBOOT_PROTOCOL)
    {
        return false;
    }

    // Walk the descriptors following the interface header to find the
    // bulk OUT endpoint.  Stop at the next interface descriptor to avoid
    // accidentally claiming endpoints that belong to a later interface on
    // composite devices.
    uint8_t const *p_desc   = tu_desc_next(itf_desc);
    uint8_t const *desc_end = (uint8_t const *) itf_desc + max_len;
    uint8_t ep_out = 0;

    while (p_desc < desc_end)
    {
        // A new interface descriptor means we have left the current
        // interface's descriptor block; stop scanning.
        if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE)
        {
            break;
        }

        if (tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT)
        {
            tusb_desc_endpoint_t const *ep_desc =
                (tusb_desc_endpoint_t const *) p_desc;

            if (tu_edpt_dir(ep_desc->bEndpointAddress) == TUSB_DIR_OUT &&
                ep_desc->bmAttributes.xfer == TUSB_XFER_BULK)
            {
                if (!tuh_edpt_open(dev_addr, ep_desc))
                {
                    printf("Failed to open bulk OUT endpoint 0x%02x on dev %d\n",
                           ep_desc->bEndpointAddress, dev_addr);
                    return false;
                }
                ep_out = ep_desc->bEndpointAddress;
                printf("Bulk OUT endpoint opened at address 0x%02x\n", ep_out);
                break;
            }
        }
        p_desc = tu_desc_next(p_desc);
    }

    if (ep_out == 0)
    {
        printf("Bulk OUT endpoint not found in Fastboot interface.\n");
        return false;
    }

    g_dev_addr = dev_addr;
    g_ep_out   = ep_out;

    return true;
}

// Called by usbh after SET_CONFIGURATION completes for our interface.
// Must call usbh_driver_set_config_complete() before returning so that
// the host stack can proceed with subsequent interface configuration.
// Once complete, start dispatching the loaded fastboot commands.
static bool fastboot_driver_set_config(uint8_t dev_addr, uint8_t itf_num)
{
    // Notify the host stack that our configuration step is done.
    usbh_driver_set_config_complete(dev_addr, itf_num);

    // Guard: this should always match the device stored in open(),
    // but be defensive in case of unexpected re-entrancy.
    if (dev_addr != g_dev_addr)
    {
        return true;
    }

    printf("Fastboot device configured. dev_addr=%d, ep_out=0x%02x\n",
           dev_addr, g_ep_out);

    if (g_cmd_count == 0)
    {
        printf("No fastboot commands to send.\n");
        return true;
    }

    printf("Sending fastboot commands to device %d's endpoint 0x%02x\n",
           dev_addr, g_ep_out);
    g_cmd_index = 0;
    send_next_cmd();

    return true;
}

// Called by usbh when a transfer on one of our claimed endpoints
// completes.  Schedule the next fastboot command after CMD_DELAY_MS.
static bool fastboot_driver_xfer_cb(uint8_t dev_addr, uint8_t ep_addr,
                                    xfer_result_t result,
                                    uint32_t xferred_bytes)
{
    (void) dev_addr;
    (void) ep_addr;

    printf("Fastboot command completed. Result: %d, xferred_bytes: %lu\n",
           result, (unsigned long) xferred_bytes);

    if (result != XFER_RESULT_SUCCESS)
    {
        printf("Transfer failed with error code %d\n", result);
        return true;
    }

    g_cmd_index++;
    if (g_cmd_index < g_cmd_count)
    {
        printf("Waiting %d ms before sending next command...\n", CMD_DELAY_MS);
        g_next_cmd_time = make_timeout_time_ms(CMD_DELAY_MS);
        g_cmd_pending   = true;
    }
    else
    {
        printf("All fastboot commands executed.\n");
    }

    return true;
}

// Called by usbh when the device is disconnected.
static void fastboot_driver_close(uint8_t dev_addr)
{
    if (g_dev_addr == dev_addr)
    {
        printf("Fastboot device removed, dev_addr=%d\n", dev_addr);
        g_dev_addr    = 0;
        g_ep_out      = 0;
        g_cmd_pending = false;
    }
}

//--------------------------------------------------------------------+
// Command dispatch
//--------------------------------------------------------------------+

// Claim the bulk OUT endpoint and queue the current command for
// transmission.  Skipped/failed commands advance the index so the
// sequence does not stall.  The function is iterative to avoid stack
// overflow when several consecutive commands need to be skipped.
static void send_next_cmd(void)
{
    while (g_ep_out != 0 && g_cmd_index < g_cmd_count)
    {
        const char *cmd     = g_cmds[g_cmd_index];
        uint32_t    cmd_len = (uint32_t) strlen(cmd);

        if (cmd_len >= MAX_CMD_LEN)
        {
            printf("Command too long (%lu bytes), skipping: %s\n",
                   (unsigned long) cmd_len, cmd);
            g_cmd_index++;
            continue;
        }

        // Copy command into the persistent transfer buffer and append the
        // null terminator required by the fastboot wire protocol.
        memcpy(g_cmd_buf, cmd, cmd_len);
        g_cmd_buf[cmd_len] = '\0';

        printf("Sending fastboot command [%d/%d]: %s\n",
               g_cmd_index + 1, g_cmd_count, cmd);

        // Claim the endpoint, then submit the transfer.  On success,
        // fastboot_driver_xfer_cb() will be invoked by the usbh stack when
        // the transfer completes — no explicit callback pointer is needed
        // because the ep2drv[] table correctly maps the endpoint to this
        // driver.
        if (!usbh_edpt_claim(g_dev_addr, g_ep_out))
        {
            printf("Failed to claim EP 0x%02x, skipping command.\n",
                   g_ep_out);
            g_cmd_index++;
            continue;
        }

        // cmd_len + 1 to include the null terminator in the transfer.
        if (!usbh_edpt_xfer(g_dev_addr, g_ep_out, g_cmd_buf, cmd_len + 1))
        {
            usbh_edpt_release(g_dev_addr, g_ep_out);
            printf("Failed to queue transfer on EP 0x%02x, skipping command.\n",
                   g_ep_out);
            g_cmd_index++;
            continue;
        }

        // Transfer successfully queued; return and wait for xfer_cb.
        return;
    }
}

//--------------------------------------------------------------------+
// Generic TinyUSB host callbacks
//--------------------------------------------------------------------+

void tuh_mount_cb(uint8_t dev_addr)
{
    printf("Device attached, address = %d\n", dev_addr);
}

void tuh_mount_failed_cb(uint8_t dev_addr)
{
    printf("Device mount failed, address = %d\n", dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr)
{
    printf("Device removed, address = %d\n", dev_addr);
}

//--------------------------------------------------------------------+
// Main
//--------------------------------------------------------------------+

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

        // Non-blocking inter-command delay: dispatch the next command once
        // the deadline set in fastboot_driver_xfer_cb has elapsed.
        if (g_cmd_pending && time_reached(g_next_cmd_time))
        {
            g_cmd_pending = false;
            send_next_cmd();
        }

        // Add a delay to reduce CPU usage
        sleep_ms(100);
    }

    return 0;
}

