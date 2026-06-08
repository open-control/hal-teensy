#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <oc/hal/teensy/SDFileSystemBackend.hpp>
#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

namespace oc::hal::teensy {

struct SDFileSystemSmokeReport {
    bool ok = false;
    uint32_t expectedCrc = 0;
    uint32_t readCrc = 0;
    size_t bytesWritten = 0;
    size_t bytesRead = 0;
    bool listedOriginal = false;
    bool listedRenamed = false;
};

namespace detail {

inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t size) {
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

inline void printError(Print& out, const char* step, const oc::type::Error& error) {
    out.print("[oc-fs-smoke] ");
    out.print(step);
    out.print(" failed: ");
    out.print(oc::type::errorCodeToString(error.code));
    if (error.context) {
        out.print(" (");
        out.print(error.context);
        out.print(")");
    }
    out.println();
}

struct ListProbe {
    const char* expectedName = nullptr;
    bool found = false;
};

inline bool listProbeVisitor(const interface::DirectoryEntry& entry, void* context) {
    auto* probe = static_cast<ListProbe*>(context);
    if (probe && probe->expectedName &&
        std::strncmp(entry.name, probe->expectedName, sizeof(entry.name)) == 0) {
        probe->found = true;
        return false;
    }
    return true;
}

template <typename T>
inline bool requireResult(Print& out, const char* step, const oc::type::Result<T>& result) {
    if (result) {
        return true;
    }
    printError(out, step, result.error());
    return false;
}

inline bool requireResult(Print& out, const char* step, const oc::type::Result<void>& result) {
    if (result) {
        return true;
    }
    printError(out, step, result.error());
    return false;
}

}  // namespace detail

inline bool runSDFileSystemSmokeTest(SDFileSystemBackend& filesystem,
                                     Print& out,
                                     SDFileSystemSmokeReport* report = nullptr) {
    SDFileSystemSmokeReport localReport{};
    auto& current = report ? *report : localReport;

    out.println("[oc-fs-smoke] start");

    auto init = filesystem.init();
    if (!detail::requireResult(out, "init", init)) {
        return false;
    }
    if (!filesystem.available()) {
        out.println("[oc-fs-smoke] SD media unavailable");
        return false;
    }

    constexpr const char* directory = "/oc-fs-smoke";
    constexpr const char* originalPath = "/oc-fs-smoke/payload.bin";
    constexpr const char* sessionPath = "/oc-fs-smoke/session.bin";
    constexpr const char* renamedPath = "/oc-fs-smoke/payload-renamed.bin";
    constexpr const char* originalName = "payload.bin";
    constexpr const char* renamedName = "payload-renamed.bin";

    auto cleanup = filesystem.remove(directory, interface::RemoveMode::RECURSIVE);
    if (!cleanup && cleanup.error().code != oc::type::ErrorCode::RESOURCE_NOT_FOUND) {
        detail::printError(out, "cleanup", cleanup.error());
        return false;
    }

    auto mkdir = filesystem.createDirectory(directory);
    if (!detail::requireResult(out, "create directory", mkdir)) {
        return false;
    }

    constexpr uint8_t payload[] = {
        0x4F, 0x43, 0x46, 0x53, 0x21, 0x00, 0x10, 0x20,
        0x30, 0x40, 0x55, 0xAA, 0xC3, 0x3C, 0x7E, 0x81,
        0x11, 0x22, 0x33, 0x44, 0x89, 0xAB, 0xCD, 0xEF,
    };

    auto write = filesystem.write(originalPath, 0, payload, sizeof(payload));
    if (!detail::requireResult(out, "write", write)) {
        return false;
    }
    current.bytesWritten = write.value();

    auto flush = filesystem.flush(originalPath);
    if (!detail::requireResult(out, "flush", flush)) {
        return false;
    }

    uint8_t buffer[sizeof(payload)] = {};
    auto read = filesystem.read(originalPath, 0, buffer, sizeof(buffer));
    if (!detail::requireResult(out, "read", read)) {
        return false;
    }
    current.bytesRead = read.value();
    current.expectedCrc = detail::crc32Update(0, payload, sizeof(payload));
    current.readCrc = detail::crc32Update(0, buffer, current.bytesRead);
    if (current.bytesRead != sizeof(payload) || current.expectedCrc != current.readCrc) {
        out.println("[oc-fs-smoke] CRC mismatch");
        return false;
    }

    constexpr uint8_t sessionPayload[] = {
        0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23,
        0x30, 0x31, 0x32, 0x33, 0x40, 0x41, 0x42, 0x43,
        0x50, 0x51, 0x52, 0x53, 0x60, 0x61, 0x62, 0x63,
        0x70, 0x71, 0x72, 0x73, 0x80, 0x81, 0x82, 0x83,
    };

    auto beginWrite = filesystem.beginWrite(sessionPath, sizeof(sessionPayload));
    if (!detail::requireResult(out, "begin session write", beginWrite)) {
        return false;
    }
    auto appendA = filesystem.appendWrite(sessionPayload, 11);
    if (!detail::requireResult(out, "append session write A", appendA)) {
        filesystem.abortWrite();
        return false;
    }
    auto appendB = filesystem.appendWrite(sessionPayload + 11, sizeof(sessionPayload) - 11);
    if (!detail::requireResult(out, "append session write B", appendB)) {
        filesystem.abortWrite();
        return false;
    }
    auto finishWrite = filesystem.finishWrite();
    if (!detail::requireResult(out, "finish session write", finishWrite)) {
        return false;
    }

    uint8_t sessionBuffer[sizeof(sessionPayload)] = {};
    auto sessionRead = filesystem.read(sessionPath, 0, sessionBuffer, sizeof(sessionBuffer));
    if (!detail::requireResult(out, "read session write", sessionRead)) {
        return false;
    }
    const uint32_t sessionExpectedCrc =
        detail::crc32Update(0, sessionPayload, sizeof(sessionPayload));
    const uint32_t sessionReadCrc =
        detail::crc32Update(0, sessionBuffer, sessionRead.value());
    if (sessionRead.value() != sizeof(sessionPayload) || sessionExpectedCrc != sessionReadCrc) {
        out.println("[oc-fs-smoke] session CRC mismatch");
        return false;
    }

    detail::ListProbe originalProbe{originalName, false};
    auto listOriginal = filesystem.list(directory, detail::listProbeVisitor, &originalProbe);
    if (!detail::requireResult(out, "list original", listOriginal)) {
        return false;
    }
    current.listedOriginal = originalProbe.found;
    if (!current.listedOriginal) {
        out.println("[oc-fs-smoke] original file not listed");
        return false;
    }

    auto rename = filesystem.rename(originalPath, renamedPath);
    if (!detail::requireResult(out, "rename", rename)) {
        return false;
    }

    detail::ListProbe renamedProbe{renamedName, false};
    auto listRenamed = filesystem.list(directory, detail::listProbeVisitor, &renamedProbe);
    if (!detail::requireResult(out, "list renamed", listRenamed)) {
        return false;
    }
    current.listedRenamed = renamedProbe.found;
    if (!current.listedRenamed) {
        out.println("[oc-fs-smoke] renamed file not listed");
        return false;
    }

    auto remove = filesystem.remove(directory, interface::RemoveMode::RECURSIVE);
    if (!detail::requireResult(out, "remove", remove)) {
        return false;
    }

    current.ok = true;
    out.print("[oc-fs-smoke] OK bytes=");
    out.print(static_cast<unsigned long>(current.bytesRead));
    out.print(" crc=0x");
    out.println(static_cast<unsigned long>(current.readCrc), HEX);
    return true;
}

}  // namespace oc::hal::teensy
