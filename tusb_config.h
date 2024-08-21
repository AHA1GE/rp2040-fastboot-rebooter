#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

// Common Configuration
#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_HOST | OPT_MODE_HIGH_SPEED)
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_DEBUG 3

// Host configuration
#define CFG_TUH_HUB 1
#define CFG_TUH_CDC 1
#define CFG_TUH_MSC 1
#define CFG_TUH_HID 0

// Memory configuration
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#endif /* _TUSB_CONFIG_H_ */
