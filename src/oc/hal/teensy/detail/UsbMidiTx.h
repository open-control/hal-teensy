#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OC_USB_MIDI_ACCEPTED,
    OC_USB_MIDI_BUSY,
    OC_USB_MIDI_OFFLINE,
    OC_USB_MIDI_ERROR
} oc_usb_midi_acceptance;

// One foreground owner, with global interrupts enabled. ACCEPTED transfers
// ownership to the SDK buffer; it does not acknowledge receipt by the host.
uint32_t oc_usb_midi_session(void);
oc_usb_midi_acceptance oc_usb_midi_try_write(uint32_t packet, uint32_t session);
void oc_usb_midi_flush(uint32_t session);

#ifdef __cplusplus
}
#endif
