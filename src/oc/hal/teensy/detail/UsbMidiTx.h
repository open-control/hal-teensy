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

// One IRQ owner, with global interrupts enabled. ACCEPTED transfers
// ownership to the SDK buffer; it does not acknowledge receipt by the host.
uint32_t oc_usb_midi_session(void);
oc_usb_midi_acceptance oc_usb_midi_try_write(uint32_t packet, uint32_t session);
void oc_usb_midi_flush(uint32_t session);
// Called on DMA retirement and reconfiguration; only pend the owner, never
// refill/render from this USB callback. nullptr detaches synchronously.
void oc_usb_midi_set_wakeup(void (*callback)(void));

#ifdef __cplusplus
}
#endif
