// Compiled after the unmodified, hash-checked Teensy SDK usb_midi.c, in the
// same translation unit. Reuse its DMA descriptors, buffers and SOF flushing.
#include "UsbMidiTx.h"

#ifdef MTP_INTERFACE
#error "Non-blocking MIDI admission has not been qualified with MTP"
#endif

static volatile uint32_t oc_usb_midi_generation;
static uint8_t oc_usb_midi_inflight;
static void (*oc_usb_midi_wakeup)(void);

static void oc_usb_midi_completed(transfer_t *transfer)
{
    // The SDK has detached completed descriptors before invoking callbacks.
    // Status==done alone is insufficient: recycling before retirement would
    // corrupt its linked list when the USB IRQ has not run yet.
    oc_usb_midi_inflight &= ~(1U << (transfer - tx_transfer));
    if (oc_usb_midi_wakeup) oc_usb_midi_wakeup();
}

void usb_midi_configure(void)
{
    oc_usb_midi_sdk_configure();
    usb_config_tx(MIDI_TX_ENDPOINT, tx_packet_size, 0, oc_usb_midi_completed);
    oc_usb_midi_inflight = 0;
    if (++oc_usb_midi_generation == 0) ++oc_usb_midi_generation;
    if (oc_usb_midi_wakeup) oc_usb_midi_wakeup();
}

uint32_t oc_usb_midi_session(void)
{
    return usb_configuration ? oc_usb_midi_generation : 0;
}

static uint32_t oc_usb_midi_lock(void)
{
    const uint32_t enabled = NVIC_IS_ENABLED(IRQ_USB1);
    NVIC_DISABLE_IRQ(IRQ_USB1);
#if defined(__arm__)
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
#endif
    return enabled;
}

static void oc_usb_midi_unlock(uint32_t enabled)
{
    if (enabled) NVIC_ENABLE_IRQ(IRQ_USB1);
}

void oc_usb_midi_set_wakeup(void (*callback)(void))
{
    const uint32_t enabled = oc_usb_midi_lock();
    oc_usb_midi_wakeup = callback;
    oc_usb_midi_unlock(enabled);
}

static void oc_usb_midi_submit(void)
{
    transfer_t *xfer = tx_transfer + tx_head;
    uint8_t *buffer = txbuffer + tx_head * TX_SIZE;
    usb_prepare_transfer(xfer, buffer, tx_packet_size - tx_available, 0);
    arm_dcache_flush_delete(buffer, TX_SIZE);
    oc_usb_midi_inflight |= 1U << tx_head;
    usb_transmit(MIDI_TX_ENDPOINT, xfer);
    tx_head = (tx_head + 1) % TX_NUM;
    tx_available = 0;
    usb_stop_sof_interrupts(MIDI_INTERFACE);
}

oc_usb_midi_acceptance oc_usb_midi_try_write(uint32_t packet, uint32_t session)
{
    const uint32_t enabled = oc_usb_midi_lock();
    oc_usb_midi_acceptance result = OC_USB_MIDI_OFFLINE;
    transfer_t *xfer = tx_transfer + tx_head;
    uint8_t *buffer = txbuffer + tx_head * TX_SIZE;
    if (!session || session != oc_usb_midi_session()) goto done;

    if (!tx_available) {
        const uint32_t status = usb_transfer_status(xfer);
        if (status & 0x68) { result = OC_USB_MIDI_ERROR; goto done; }
        if ((status & 0x80) || (oc_usb_midi_inflight & (1U << tx_head))) {
            result = OC_USB_MIDI_BUSY; goto done;
        }
        tx_available = tx_packet_size;
    }

    tx_noautoflush = 1;
    memcpy(buffer + tx_packet_size - tx_available, &packet, sizeof(packet));
    tx_available -= sizeof(packet);
    if (!tx_available) {
        oc_usb_midi_submit();
    } else {
        usb_start_sof_interrupts(MIDI_INTERFACE);
    }
    tx_noautoflush = 0;
    result = OC_USB_MIDI_ACCEPTED;
done:
    oc_usb_midi_unlock(enabled);
    return result;
}

void oc_usb_midi_flush(uint32_t session)
{
    const uint32_t enabled = oc_usb_midi_lock();
    if (session && session == oc_usb_midi_session() && !tx_noautoflush && tx_available)
        oc_usb_midi_submit();
    oc_usb_midi_unlock(enabled);
}

// USB SOF uses the same descriptor bookkeeping as explicit owner flushing.
void usb_midi_flush_output(void)
{
    oc_usb_midi_flush(oc_usb_midi_session());
}
