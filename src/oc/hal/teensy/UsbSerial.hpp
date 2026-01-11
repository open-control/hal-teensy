#pragma once

/**
 * @file UsbSerial.hpp
 * @brief Teensy USB Serial transport with COBS framing
 *
 * Implements ISerialTransport using Teensy's USB Serial.
 * Handles COBS encoding/decoding for reliable frame delivery.
 *
 * @note Requires USB Type "Serial + MIDI" or "Serial" in Arduino IDE.
 *       Baud rate is ignored for USB Serial (always full USB speed).
 */

#include <Arduino.h>
#include <oc/hal/ISerialTransport.hpp>
#include <oc/codec/CobsCodec.hpp>

namespace oc::hal::teensy {

/**
 * @brief USB Serial transport configuration
 */
struct UsbSerialConfig {
    /// Maximum frame size (must match bridge)
    size_t maxFrameSize = codec::COBS_MAX_FRAME_SIZE;
};

/**
 * @brief Teensy USB Serial driver with COBS framing
 *
 * Provides reliable framed communication over USB Serial.
 * Compatible with oc-bridge for TCP tunneling.
 */
class UsbSerial : public hal::ISerialTransport {
public:
    UsbSerial() = default;
    explicit UsbSerial(const UsbSerialConfig& config)
        : maxFrameSize_(config.maxFrameSize) {}

    ~UsbSerial() override = default;

    UsbSerial(const UsbSerial&) = delete;
    UsbSerial& operator=(const UsbSerial&) = delete;

    /**
     * @brief Initialize USB Serial
     *
     * @note Baud rate is ignored for USB Serial (native USB speed).
     */
    core::Result<void> init() override {
        Serial.begin(0);  // Baud ignored for USB
        initialized_ = true;
        return core::Result<void>::ok();
    }

    /**
     * @brief Poll for incoming data
     *
     * Reads available bytes from USB Serial, decodes COBS frames,
     * and invokes the receive callback for each complete frame.
     */
    void update() override {
        if (!initialized_ || !onReceive_) return;

        while (Serial.available()) {
            uint8_t byte = Serial.read();
            decoder_.feed(byte, [this](const uint8_t* data, size_t len) {
                onReceive_(data, len);
            });
        }
    }

    /**
     * @brief Send a framed message
     *
     * COBS-encodes the data and transmits over USB Serial.
     *
     * @param data Pointer to message data
     * @param length Number of bytes to send
     */
    void send(const uint8_t* data, size_t length) override {
        if (!initialized_ || length == 0) return;
        if (length > maxFrameSize_) return;  // Frame too large

        // COBS encode
        uint8_t encoded[codec::cobsMaxEncodedSize(length)];
        size_t encodedLen = codec::cobsEncode(data, length, encoded);

        // Transmit
        Serial.write(encoded, encodedLen);
    }

    /**
     * @brief Set callback for received frames
     */
    void setOnReceive(ReceiveCallback cb) override {
        onReceive_ = cb;
    }

    /**
     * @brief Check if Serial Monitor is connected
     * @return true if USB Serial is active
     */
    bool isConnected() const {
        return static_cast<bool>(Serial);
    }

private:
    ReceiveCallback onReceive_;
    codec::CobsDecoder<> decoder_;
    size_t maxFrameSize_ = codec::COBS_MAX_FRAME_SIZE;
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
