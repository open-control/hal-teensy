// Exercise the production extension against deterministic DMA/IRQ state.
#include <cassert>
#include <cstdint>
#include <cstring>

#define TX_NUM 4
#define TX_SIZE 512
#define MIDI_INTERFACE 1
#define MIDI_TX_ENDPOINT 3
#define IRQ_USB1 0
static bool irqEnabled = true;
#define NVIC_IS_ENABLED(irq) irqEnabled
#define NVIC_DISABLE_IRQ(irq) (irqEnabled = false)
#define NVIC_ENABLE_IRQ(irq) (irqEnabled = true)

struct transfer_t { uint32_t status = 0; uint32_t length = 0; };
static transfer_t tx_transfer[TX_NUM];
static uint8_t txbuffer[TX_NUM * TX_SIZE];
static uint8_t tx_head, tx_noautoflush;
static uint16_t tx_available, tx_packet_size;
static uint8_t usb_configuration;
static unsigned statusReads, submissions;
static bool sofEnabled;

static uint32_t usb_transfer_status(transfer_t* t) {
    assert(!irqEnabled); ++statusReads; return t->status;
}
static void usb_prepare_transfer(transfer_t* t, void*, uint32_t size, int) { t->length = size; }
static void arm_dcache_flush_delete(void*, uint32_t size) { assert(size == TX_SIZE); }
static void usb_transmit(int, transfer_t* t) {
    assert(!irqEnabled); ++submissions; t->status = 0x80;
}
static void usb_start_sof_interrupts(int) { sofEnabled = true; }
static void usb_stop_sof_interrupts(int) { sofEnabled = false; }
static void oc_usb_midi_sdk_configure() {
    tx_head = tx_noautoflush = 0; tx_available = 0; tx_packet_size = TX_SIZE;
    for (auto& t : tx_transfer) t = {};
}
static void usb_midi_flush_output() {
    assert(!irqEnabled);
    if (!tx_noautoflush && tx_available) {
        auto* t = &tx_transfer[tx_head];
        t->length = tx_packet_size - tx_available;
        usb_transmit(MIDI_TX_ENDPOINT, t);
        tx_head = (tx_head + 1) % TX_NUM;
        tx_available = 0; sofEnabled = false;
    }
}

#include <oc/hal/teensy/detail/UsbMidiTx.inc>

int main() {
    assert(oc_usb_midi_session() == 0);
    assert(oc_usb_midi_try_write(0xF80F, 0) == OC_USB_MIDI_OFFLINE);
    assert(irqEnabled && !tx_noautoflush);
    usb_configuration = 1;
    usb_midi_configure();
    const uint32_t session = oc_usb_midi_session();
    assert(session);
    for (auto& t : tx_transfer) t.status = 0x80;
    for (unsigned i = 0; i < 100; ++i) {
        assert(oc_usb_midi_try_write(0xF80F, session) == OC_USB_MIDI_BUSY);
        assert(irqEnabled && !tx_noautoflush && !tx_available && !tx_head);
    }
    assert(statusReads == 100 && submissions == 0);
    tx_transfer[0].status = 0x40;
    assert(oc_usb_midi_try_write(0xF80F, session) == OC_USB_MIDI_ERROR);
    assert(irqEnabled && !tx_noautoflush && !tx_available);
    for (auto& t : tx_transfer) t.status = 0;
    assert(oc_usb_midi_try_write(0xF80F, session) == OC_USB_MIDI_ACCEPTED);
    assert(tx_available == 508 && sofEnabled);
    uint32_t stored = 0;
    std::memcpy(&stored, txbuffer, sizeof(stored));
    assert(stored == 0xF80F);
    oc_usb_midi_flush(session);
    assert(tx_transfer[0].length == 4 && tx_head == 1 && submissions == 1);
    assert(!tx_available && !sofEnabled && irqEnabled);
    for (unsigned i = 0; i < TX_SIZE / 4; ++i) {
        assert(oc_usb_midi_try_write(0x643C9009, session) == OC_USB_MIDI_ACCEPTED);
    }
    assert(tx_transfer[1].length == 512 && tx_head == 2 && submissions == 2);
    assert(!sofEnabled && !tx_noautoflush);
    irqEnabled = false;
    assert(oc_usb_midi_try_write(0xF80F, session) == OC_USB_MIDI_ACCEPTED);
    assert(!irqEnabled); // Preserve a caller's USB IRQ mask, not global PRIMASK.
    oc_usb_midi_flush(session);
    assert(!irqEnabled);
    irqEnabled = true;
    usb_configuration = 0;
    const auto beforeDisconnect = submissions;
    assert(oc_usb_midi_try_write(0xF80F, session) == OC_USB_MIDI_OFFLINE);
    oc_usb_midi_flush(session);
    assert(submissions == beforeDisconnect && irqEnabled);
    usb_configuration = 1;
    usb_midi_configure();
    assert(oc_usb_midi_session() != session);
    assert(oc_usb_midi_try_write(0xF80F, session) == OC_USB_MIDI_OFFLINE);
    tx_packet_size = 64; // Full-speed USB uses the same descriptor pool.
    for (unsigned i = 0; i < 4 * 16; ++i) {
        assert(oc_usb_midi_try_write(0xF80F, oc_usb_midi_session()) == OC_USB_MIDI_ACCEPTED);
    }
    assert(tx_head == 0 && !tx_available);
    assert(oc_usb_midi_try_write(0xF80F, oc_usb_midi_session()) == OC_USB_MIDI_BUSY);
    for (auto& t : tx_transfer) assert(t.length == 64);
    oc_usb_midi_generation = UINT32_MAX;
    usb_midi_configure();
    assert(oc_usb_midi_session() == 1); // Zero remains reserved for offline.
}
