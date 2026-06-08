#pragma once

#include <SD.h>

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

namespace oc::hal::teensy {

/**
 * Teensy 4.1 SD-backed filesystem for user-visible file trees.
 *
 * This backend implements IFileSystem directly on top of the built-in SDIO
 * slot. It keeps no persistent file handles and allocates no dynamic memory;
 * callers own transfer buffers and directory entries are streamed by visitor.
 */
class SDFileSystemBackend : public interface::IFileSystem {
public:
    oc::type::Result<void> init() override;
    bool available() const override;

    oc::type::Result<interface::FileInfo> stat(const char* path) override;
    oc::type::Result<void> list(const char* path,
                                interface::DirectoryEntryVisitor visitor,
                                void* context) override;
    oc::type::Result<void> createDirectory(const char* path) override;
    oc::type::Result<void> remove(
        const char* path,
        interface::RemoveMode mode = interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    ) override;
    oc::type::Result<void> rename(const char* fromPath, const char* toPath) override;
    oc::type::Result<size_t> read(const char* path,
                                  uint32_t offset,
                                  uint8_t* buffer,
                                  size_t size) override;
    oc::type::Result<size_t> write(const char* path,
                                   uint32_t offset,
                                   const uint8_t* data,
                                   size_t size) override;
    oc::type::Result<void> flush(const char* path) override;
    oc::type::Result<void> beginWrite(const char* path, uint32_t expectedSize) override;
    oc::type::Result<size_t> appendWrite(const uint8_t* data, size_t size) override;
    oc::type::Result<void> finishWrite() override;
    void abortWrite() override;

private:
    static constexpr size_t PATH_BUFFER_SIZE = interface::FILESYSTEM_MAX_PATH_LENGTH + 1;
    static constexpr uint8_t MAX_REMOVE_DEPTH = 8;

    oc::type::Result<void> ensureAvailable_() const;
    oc::type::Result<void> normalizePath_(const char* path, char* out, size_t outSize) const;
    static bool isRoot_(const char* path);
    static oc::type::Result<void> parentPath_(const char* path, char* out, size_t outSize);
    static interface::FileType typeOf_(const FsFile& file);
    static void fillInfo_(const FsFile& file, interface::FileInfo& info);
    static void fillEntry_(FsFile& file, interface::DirectoryEntry& entry);
    oc::type::Result<void> removePath_(const char* path, bool recursive, uint8_t depth);
    static bool joinPath_(const char* parent, const char* name, char* out, size_t outSize);
    void resetWriteStream_();

    FsFile writeStream_;
    uint32_t writeExpectedSize_ = 0;
    uint32_t writeBytes_ = 0;
    bool writeActive_ = false;
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
