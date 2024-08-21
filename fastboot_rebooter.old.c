#include "pico/stdlib.h"
#include "tusb.h"

#define FASTBOOT_CMD "reboot\x00" // Fastboot only send ascii chars, every cmd should end with a null char

uint ready_LED_pin = 25; // The on-board LED pin
uint busy_LED_pin = 14;  // Change to the GPIO pin number you are using

void send_fastboot_reboot(uint8_t dev_addr, uint8_t ep_addr);
void transfer_complete_cb(tuh_xfer_t *xfer);
void control_transfer_complete_cb(tuh_xfer_t *xfer);
void descriptor_complete_cb(tuh_xfer_t *xfer);

void set_led_fail()
{
    // turn off both LED for failure
    gpio_put(ready_LED_pin, 0);
    gpio_put(busy_LED_pin, 0);
    // Sleep for 3 second to indicate failure
    sleep_ms(3000);
}

void set_led_error()
{
    // turn on both LED for error
    gpio_put(ready_LED_pin, 1);
    gpio_put(busy_LED_pin, 1);
    // Sleep for 3 second to indicate error
    sleep_ms(3000);
}

void send_fastboot_reboot(uint8_t dev_addr, uint8_t ep_addr)
{
    tuh_xfer_t xfer;
    uint8_t cmd[] = FASTBOOT_CMD;

    // Initialize the transfer structure
    xfer.daddr = dev_addr;                   // Device address
    xfer.ep_addr = ep_addr;                  // Endpoint address
    xfer.result = 0;                         // Initialize result, may be updated by the transfer
    xfer.actual_len = 0;                     // Actual length of data transferred, initialized to 0
    xfer.buffer = cmd;                       // Pointer to the data buffer
    xfer.complete_cb = transfer_complete_cb; // Callback function
    xfer.user_data = 0;                      // User data, initialized to 0

    // Setup the union for bulk transfer
    xfer.buflen = sizeof(cmd);

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
        set_led_fail();
    }
}

void transfer_complete_cb(tuh_xfer_t *xfer)
{
    if (xfer->result == XFER_RESULT_SUCCESS)
    {
        printf("Transfer completed successfully\n");
    }
    else
    {
        printf("Transfer failed with result: %d\n", xfer->result);
        set_led_fail();
    }

    // Turn off busy LED and turn on ready LED
    gpio_put(busy_LED_pin, 0);
    gpio_put(ready_LED_pin, 1);
}

void descriptor_complete_cb(tuh_xfer_t *xfer)
{
    if (xfer->result == XFER_RESULT_SUCCESS)
    {
        uint8_t const *p_desc = (uint8_t const *)xfer->buffer;
        uint8_t const *p_end = p_desc + ((tusb_desc_configuration_t const *)p_desc)->wTotalLength;

        uint8_t ep_addr = 0;
        while (p_desc < p_end)
        {
            if (TUSB_DESC_ENDPOINT == tu_desc_type(p_desc))
            {
                tusb_desc_endpoint_t const *p_endpoint = (tusb_desc_endpoint_t const *)p_desc;
                if (TUSB_XFER_BULK == p_endpoint->bmAttributes.xfer && !tu_edpt_dir(p_endpoint->bEndpointAddress))
                {
                    ep_addr = p_endpoint->bEndpointAddress;
                    printf("Found bulk OUT endpoint: 0x%02X\n", ep_addr);
                    break;
                }
            }
            p_desc = tu_desc_next(p_desc);
        }

        if (ep_addr)
        {
            send_fastboot_reboot(xfer->daddr, ep_addr);
        }
        else
        {
            printf("Failed to find bulk OUT endpoint\n");
            set_led_error();
        }
    }
    else
    {
        printf("Failed to retrieve configuration descriptor\n");
        set_led_error();
    }
}

void tuh_mount_cb(uint8_t dev_addr)
{
    printf("Device mounted: address %d\n", dev_addr);

    // Turn on busy LED and turn off ready LED
    gpio_put(busy_LED_pin, 1);
    gpio_put(ready_LED_pin, 0);

    // Buffer to hold the configuration descriptor
    static uint8_t config_descriptor[256];

    // Retrieve the configuration descriptor asynchronously
    if (!tuh_descriptor_get_configuration(dev_addr, 0, config_descriptor, sizeof(config_descriptor), descriptor_complete_cb, 0))
    {
        printf("Failed to initiate configuration descriptor retrieval\n");
        set_led_error();
    }
}

void tuh_mount_failed_cb(uint8_t dev_addr)
{
    printf("Device mount failed: address %d\n", dev_addr);
    // blink busy LED to indicate cannot mount the device
    gpio_put(busy_LED_pin, 1);
    sleep_ms(500);
    gpio_put(busy_LED_pin, 0);
    sleep_ms(500);
    gpio_put(busy_LED_pin, 1);
    sleep_ms(500);
    gpio_put(busy_LED_pin, 0);
}

void tuh_umount_cb(uint8_t dev_addr)
{
    printf("Device unmounted: address %d\n", dev_addr);
    // turn on ready LED and turn off busy LED
    gpio_put(ready_LED_pin, 1);
    gpio_put(busy_LED_pin, 0);
}

int main()
{
    printf("waiting 3s for UART ...\n");
    sleep_ms(3000); // Sleep for 3 seconds for UART initialization
    printf("init stdio ...\n");
    stdio_init_all();

    printf("init USB ...\n");
    // Initialize the TinyUSB host stack with one host controller
    if (!tuh_init(TUH_OPT_RHPORT))
    {
        printf("Failed to initialize TinyUSB host stack\n");
        return -1;
    }

    printf("init LEDs ...\n");
    // Initialize the GPIO pins for the LEDs
    gpio_init(ready_LED_pin);
    gpio_init(busy_LED_pin);
    gpio_set_dir(ready_LED_pin, GPIO_OUT);
    gpio_set_dir(busy_LED_pin, GPIO_OUT);
    // Turn on ready LED and turn off busy LED
    gpio_put(ready_LED_pin, 1);
    gpio_put(busy_LED_pin, 0);

    printf("init DONE!\n");

    while (true)
    {
        tuh_task();

        // Add a delay to reduce CPU usage
        sleep_ms(100); // Sleep for 100 milliseconds
    }

    return 0;
}
