#include "SDFileSystemBackend.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "BuiltInSD.hpp"

namespace oc::hal::teensy {

namespace {

constexpr size_t SDIO_PSRAM_STAGING_SIZE = 512U;

FLASHMEM bool isExternalRamAddress(const void* address) {
#if defined(__IMXRT1062__)
    return (reinterpret_cast<uintptr_t>(address) >> 28U) == 0x7U;
#else
    (void)address;
    return false;
#endif
}

/**
 * The Teensy SDIO DMA path cannot safely consume every FlexSPI PSRAM span used
 * by the full product image. Stage only external-memory sources through one
 * native SD sector in DTCM; internal-memory callers keep the zero-copy path.
 */
FLASHMEM size_t writeStable(FsFile& file, const uint8_t* data, size_t size) {
    if (!isExternalRamAddress(data)) {
        return file.write(data, size);
    }

    alignas(4) uint8_t staging[SDIO_PSRAM_STAGING_SIZE];
    size_t total = 0;
    while (total < size) {
        const size_t remaining = size - total;
        const size_t chunk = remaining < sizeof(staging) ? remaining : sizeof(staging);
        std::memcpy(staging, data + total, chunk);
        const size_t written = file.write(staging, chunk);
        total += written;
        if (written != chunk) break;
    }
    return total;
}

}  // namespace

FLASHMEM oc::type::Result<void> SDFileSystemBackend::init() {
    if (initialized_) {
        if (available()) return oc::type::Result<void>::ok();
        if (detail::reinitializeBuiltInSD()) {
            return oc::type::Result<void>::ok();
        }
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::HARDWARE_INIT_FAILED, "SDIO DMA reopen failed"}
        );
    }
    if (!detail::initializeBuiltInSD()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::HARDWARE_INIT_FAILED, "SDIO DMA init failed"}
        );
    }

    initialized_ = true;
    return oc::type::Result<void>::ok();
}

FLASHMEM bool SDFileSystemBackend::available() const {
    return initialized_ && detail::builtInSDMediaPresent();
}

FLASHMEM oc::type::Result<interface::FileInfo> SDFileSystemBackend::stat(const char* path) {
    char normalized[PATH_BUFFER_SIZE] = {};
    auto pathResult = normalizePath_(path, normalized, sizeof(normalized));
    if (!pathResult) {
        return oc::type::Result<interface::FileInfo>::err(pathResult.error());
    }

    FsFile file = SD.sdfs.open(normalized, O_RDONLY);
    if (!file.isOpen()) {
        return oc::type::Result<interface::FileInfo>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "path not found"}
        );
    }

    interface::FileInfo info{};
    fillInfo_(file, info);
    file.close();
    return oc::type::Result<interface::FileInfo>::ok(info);
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::list(
    const char* path,
    interface::DirectoryEntryVisitor visitor,
    void* context
) {
    if (!visitor) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "null directory visitor"}
        );
    }

    char normalized[PATH_BUFFER_SIZE] = {};
    auto pathResult = normalizePath_(path, normalized, sizeof(normalized));
    if (!pathResult) {
        return pathResult;
    }

    FsFile directory = SD.sdfs.open(normalized, O_RDONLY);
    if (!directory.isOpen()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "directory not found"}
        );
    }
    if (!directory.isDir()) {
        directory.close();
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "path is not a directory"}
        );
    }

    FsFile child;
    while (child.openNext(&directory, O_RDONLY)) {
        interface::DirectoryEntry entry{};
        fillEntry_(child, entry);
        const bool shouldContinue = visitor(entry, context);
        child.close();
        if (!shouldContinue) {
            break;
        }
    }

    directory.close();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::createDirectory(const char* path) {
    char normalized[PATH_BUFFER_SIZE] = {};
    auto pathResult = normalizePath_(path, normalized, sizeof(normalized));
    if (!pathResult) {
        return pathResult;
    }

    if (isRoot_(normalized)) {
        return oc::type::Result<void>::ok();
    }

    FsFile existing = SD.sdfs.open(normalized, O_RDONLY);
    if (existing.isOpen()) {
        const bool isDirectory = existing.isDir();
        existing.close();
        if (!isDirectory) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::INVALID_STATE, "path exists and is not a directory"}
            );
        }
        return oc::type::Result<void>::ok();
    }

    if (!SD.sdfs.mkdir(normalized, true)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "create directory failed"}
        );
    }

    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::remove(
    const char* path,
    interface::RemoveMode mode
) {
    char normalized[PATH_BUFFER_SIZE] = {};
    auto pathResult = normalizePath_(path, normalized, sizeof(normalized));
    if (!pathResult) {
        return pathResult;
    }
    if (isRoot_(normalized)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "cannot remove filesystem root"}
        );
    }

    return removePath_(normalized, mode == interface::RemoveMode::RECURSIVE, 0);
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::rename(
    const char* fromPath,
    const char* toPath
) {
    char fromNormalized[PATH_BUFFER_SIZE] = {};
    auto fromResult = normalizePath_(fromPath, fromNormalized, sizeof(fromNormalized));
    if (!fromResult) {
        return fromResult;
    }
    if (isRoot_(fromNormalized)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "cannot rename filesystem root"}
        );
    }

    char toNormalized[PATH_BUFFER_SIZE] = {};
    auto toResult = normalizePath_(toPath, toNormalized, sizeof(toNormalized));
    if (!toResult) {
        return toResult;
    }
    if (isRoot_(toNormalized)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "target cannot be filesystem root"}
        );
    }

    FsFile source = SD.sdfs.open(fromNormalized, O_RDONLY);
    if (!source.isOpen()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "source not found"}
        );
    }
    source.close();

    FsFile target = SD.sdfs.open(toNormalized, O_RDONLY);
    if (target.isOpen()) {
        target.close();
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "target already exists"}
        );
    }

    char parent[PATH_BUFFER_SIZE] = {};
    auto parentResult = parentPath_(toNormalized, parent, sizeof(parent));
    if (!parentResult) {
        return parentResult;
    }

    FsFile parentDirectory = SD.sdfs.open(parent, O_RDONLY);
    if (!parentDirectory.isOpen()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "target parent not found"}
        );
    }
    const bool parentIsDirectory = parentDirectory.isDir();
    parentDirectory.close();
    if (!parentIsDirectory) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "target parent is not a directory"}
        );
    }

    if (!SD.sdfs.rename(fromNormalized, toNormalized)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "rename failed"}
        );
    }

    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<size_t> SDFileSystemBackend::read(
    const char* path,
    uint32_t offset,
    uint8_t* buffer,
    size_t size
) {
    if (!buffer && size > 0) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "null read buffer"}
        );
    }

    char normalized[PATH_BUFFER_SIZE] = {};
    auto pathResult = normalizePath_(path, normalized, sizeof(normalized));
    if (!pathResult) {
        return oc::type::Result<size_t>::err(pathResult.error());
    }

    FsFile file = SD.sdfs.open(normalized, O_RDONLY);
    if (!file.isOpen()) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "file not found"}
        );
    }
    if (!file.isFile()) {
        file.close();
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "path is not a file"}
        );
    }

    const uint64_t fileSize = file.fileSize();
    if (offset >= fileSize || size == 0) {
        file.close();
        return oc::type::Result<size_t>::ok(0);
    }

    const uint64_t remaining = fileSize - offset;
    const size_t maxRead = remaining < size ? static_cast<size_t>(remaining) : size;
    if (!file.seekSet(offset)) {
        file.close();
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::STORAGE_READ_FAILED, "seek read failed"}
        );
    }

    const int readBytes = file.read(buffer, maxRead);
    file.close();
    if (readBytes < 0) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::STORAGE_READ_FAILED, "read failed"}
        );
    }

    return oc::type::Result<size_t>::ok(static_cast<size_t>(readBytes));
}

FLASHMEM oc::type::Result<size_t> SDFileSystemBackend::write(
    const char* path,
    uint32_t offset,
    const uint8_t* data,
    size_t size
) {
    if (!data && size > 0) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "null write buffer"}
        );
    }

    char normalized[PATH_BUFFER_SIZE] = {};
    auto pathResult = normalizePath_(path, normalized, sizeof(normalized));
    if (!pathResult) {
        return oc::type::Result<size_t>::err(pathResult.error());
    }
    if (isRoot_(normalized)) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "cannot write filesystem root"}
        );
    }

    char parent[PATH_BUFFER_SIZE] = {};
    auto parentResult = parentPath_(normalized, parent, sizeof(parent));
    if (!parentResult) {
        return oc::type::Result<size_t>::err(parentResult.error());
    }

    FsFile parentDirectory = SD.sdfs.open(parent, O_RDONLY);
    if (!parentDirectory.isOpen()) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "parent directory not found"}
        );
    }
    const bool parentIsDirectory = parentDirectory.isDir();
    parentDirectory.close();
    if (!parentIsDirectory) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "parent is not a directory"}
        );
    }

    uint64_t currentSize = 0;
    FsFile existing = SD.sdfs.open(normalized, O_RDONLY);
    if (existing.isOpen()) {
        if (!existing.isFile()) {
            existing.close();
            return oc::type::Result<size_t>::err(
                {oc::type::ErrorCode::INVALID_ARGUMENT, "path is a directory"}
            );
        }
        currentSize = existing.fileSize();
        existing.close();
    }

    if (offset > currentSize) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "write gap not allowed"}
        );
    }
    if (size == 0) {
        return oc::type::Result<size_t>::ok(0);
    }

    FsFile file = SD.sdfs.open(normalized, O_RDWR | O_CREAT);
    if (!file.isOpen()) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "open write failed"}
        );
    }
    if (!file.seekSet(offset)) {
        file.close();
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "seek write failed"}
        );
    }

    const size_t written = writeStable(file, data, size);
    const bool synced = file.sync();
    file.close();
    if (written != size || !synced) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "write failed"}
        );
    }

    return oc::type::Result<size_t>::ok(written);
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::flush(const char* path) {
    char normalized[PATH_BUFFER_SIZE] = {};
    auto pathResult = normalizePath_(path, normalized, sizeof(normalized));
    if (!pathResult) {
        return pathResult;
    }

    FsFile file = SD.sdfs.open(normalized, O_RDWR);
    if (!file.isOpen()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "path not found"}
        );
    }
    if (!file.isFile()) {
        file.close();
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "path is not a file"}
        );
    }

    const bool synced = file.sync();
    file.close();
    if (!synced) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "flush failed"}
        );
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::beginWrite(
    const char* path,
    uint32_t expectedSize
) {
    if (writeActive_) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "write stream already active"}
        );
    }

    char normalized[PATH_BUFFER_SIZE] = {};
    auto pathResult = normalizePath_(path, normalized, sizeof(normalized));
    if (!pathResult) {
        return pathResult;
    }
    if (isRoot_(normalized)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "cannot write filesystem root"}
        );
    }

    char parent[PATH_BUFFER_SIZE] = {};
    auto parentResult = parentPath_(normalized, parent, sizeof(parent));
    if (!parentResult) {
        return parentResult;
    }

    FsFile parentDirectory = SD.sdfs.open(parent, O_RDONLY);
    if (!parentDirectory.isOpen()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "parent directory not found"}
        );
    }
    const bool parentIsDirectory = parentDirectory.isDir();
    parentDirectory.close();
    if (!parentIsDirectory) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "parent is not a directory"}
        );
    }

    FsFile existing = SD.sdfs.open(normalized, O_RDONLY);
    if (existing.isOpen()) {
        const bool isFile = existing.isFile();
        existing.close();
        if (!isFile) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::INVALID_ARGUMENT, "path is a directory"}
            );
        }
        if (!SD.sdfs.remove(normalized)) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "replace existing file failed"}
            );
        }
    }

    if (!writeStream_.open(normalized, O_RDWR | O_CREAT | O_TRUNC)) {
        OC_LOG_ERROR(
            "[SDFileSystem] open stream failed sdError={} sdData={}",
            SD.sdfs.sdErrorCode(),
            SD.sdfs.sdErrorData()
        );
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "open write stream failed"}
        );
    }
    if (expectedSize > 0) {
        const bool preallocated = writeStream_.preAllocate(expectedSize);
        const bool rewound = preallocated && writeStream_.seekSet(0);
        if (!preallocated || !rewound) {
            OC_LOG_ERROR(
                "[SDFileSystem] prepare stream failed bytes={} prealloc={} rewind={} "
                "writeError={} sdError={} sdData={}",
                expectedSize,
                preallocated,
                rewound,
                writeStream_.getWriteError(),
                SD.sdfs.sdErrorCode(),
                SD.sdfs.sdErrorData()
            );
            writeStream_.close();
            resetWriteStream_();
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED,
                 preallocated ? "rewind write stream failed"
                              : "preallocate write stream failed"}
            );
        }
    }

    writeExpectedSize_ = expectedSize;
    writeBytes_ = 0;
    writeActive_ = true;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<size_t> SDFileSystemBackend::appendWrite(
    const uint8_t* data,
    size_t size
) {
    if (!data && size > 0) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "null write buffer"}
        );
    }
    if (!writeActive_ || !writeStream_.isOpen()) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_STATE, "write stream is not active"}
        );
    }
    if (writeBytes_ + size > writeExpectedSize_) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "write exceeds expected size"}
        );
    }
    if (size == 0) {
        return oc::type::Result<size_t>::ok(0);
    }

    const uint64_t position = writeStream_.position();
    if (position != writeBytes_ && !writeStream_.seekSet(writeBytes_)) {
        OC_LOG_ERROR(
            "[SDFileSystem] seek stream failed expected={} actual={} sdError={} sdData={}",
            writeBytes_,
            static_cast<uint32_t>(position),
            SD.sdfs.sdErrorCode(),
            SD.sdfs.sdErrorData()
        );
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "seek write stream failed"}
        );
    }

    const size_t written = writeStable(writeStream_, data, size);
    if (written != size) {
        OC_LOG_ERROR(
            "[SDFileSystem] append stream failed requested={} written={} offset={} "
            "position={} fileSize={} writeError={} sdError={} sdData={}",
            size,
            written,
            writeBytes_,
            static_cast<uint32_t>(writeStream_.position()),
            static_cast<uint32_t>(writeStream_.fileSize()),
            writeStream_.getWriteError(),
            SD.sdfs.sdErrorCode(),
            SD.sdfs.sdErrorData()
        );
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "write stream failed"}
        );
    }

    writeBytes_ += written;
    return oc::type::Result<size_t>::ok(written);
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::finishWrite() {
    if (!writeActive_ || !writeStream_.isOpen()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "write stream is not active"}
        );
    }
    if (writeBytes_ != writeExpectedSize_) {
        abortWrite();
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "write stream size mismatch"}
        );
    }

    const bool truncated = writeStream_.truncate(writeExpectedSize_);
    const bool synced = truncated && writeStream_.sync();
    writeStream_.close();
    resetWriteStream_();
    if (!synced) {
        OC_LOG_ERROR(
            "[SDFileSystem] finish stream failed truncate={} sdError={} sdData={}",
            truncated,
            SD.sdfs.sdErrorCode(),
            SD.sdfs.sdErrorData()
        );
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "finish write stream failed"}
        );
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM void SDFileSystemBackend::abortWrite() {
    if (writeStream_.isOpen()) {
        writeStream_.close();
    }
    resetWriteStream_();
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::ensureAvailable_() const {
    if (!initialized_) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "filesystem not initialized"}
        );
    }
    if (!detail::builtInSDMediaPresent()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::HARDWARE_NOT_FOUND, "SD media not present"}
        );
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::normalizePath_(
    const char* path,
    char* out,
    size_t outSize
) const {
    auto availability = ensureAvailable_();
    if (!availability) {
        return availability;
    }
    if (!path || path[0] == '\0' || !out || outSize == 0) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "empty path"}
        );
    }

    size_t write = 0;
    out[write++] = '/';

    size_t read = 0;
    while (path[read] == '/') {
        ++read;
    }

    while (path[read] != '\0') {
        size_t segmentLength = 0;
        const size_t segmentStart = read;
        while (path[read] != '\0' && path[read] != '/') {
            const char c = path[read];
            if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 32U ||
                static_cast<unsigned char>(c) == 127U) {
                return oc::type::Result<void>::err(
                    {oc::type::ErrorCode::INVALID_ARGUMENT, "invalid path character"}
                );
            }
            ++segmentLength;
            ++read;
        }

        if (segmentLength > interface::FILESYSTEM_MAX_NAME_LENGTH) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::INVALID_ARGUMENT, "path segment too long"}
            );
        }
        if ((segmentLength == 1 && path[segmentStart] == '.') ||
            (segmentLength == 2 && path[segmentStart] == '.' &&
             path[segmentStart + 1] == '.')) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::INVALID_ARGUMENT, "dot segment not allowed"}
            );
        }

        if (segmentLength > 0) {
            if (write > 1) {
                if (write + 1 >= outSize) {
                    return oc::type::Result<void>::err(
                        {oc::type::ErrorCode::INVALID_ARGUMENT, "path too long"}
                    );
                }
                out[write++] = '/';
            }
            if (write + segmentLength >= outSize) {
                return oc::type::Result<void>::err(
                    {oc::type::ErrorCode::INVALID_ARGUMENT, "path too long"}
                );
            }
            std::memcpy(out + write, path + segmentStart, segmentLength);
            write += segmentLength;
        }

        while (path[read] == '/') {
            ++read;
        }
    }

    out[write] = '\0';
    return oc::type::Result<void>::ok();
}

FLASHMEM bool SDFileSystemBackend::isRoot_(const char* path) {
    return path && path[0] == '/' && path[1] == '\0';
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::parentPath_(
    const char* path,
    char* out,
    size_t outSize
) {
    if (!path || !out || outSize == 0 || isRoot_(path)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "invalid parent path"}
        );
    }

    size_t lastSlash = 0;
    for (size_t index = 1; path[index] != '\0'; ++index) {
        if (path[index] == '/') {
            lastSlash = index;
        }
    }

    if (lastSlash == 0) {
        if (outSize < 2) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::INVALID_ARGUMENT, "parent path buffer too small"}
            );
        }
        out[0] = '/';
        out[1] = '\0';
        return oc::type::Result<void>::ok();
    }

    if (lastSlash + 1 > outSize) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "parent path too long"}
        );
    }
    std::memcpy(out, path, lastSlash);
    out[lastSlash] = '\0';
    return oc::type::Result<void>::ok();
}

FLASHMEM interface::FileType SDFileSystemBackend::typeOf_(const FsFile& file) {
    if (file.isFile()) {
        return interface::FileType::FILE;
    }
    if (file.isDir()) {
        return interface::FileType::DIRECTORY;
    }
    return interface::FileType::OTHER;
}

FLASHMEM void SDFileSystemBackend::fillInfo_(const FsFile& file, interface::FileInfo& info) {
    info.type = typeOf_(file);
    info.sizeBytes = 0;
    if (info.type == interface::FileType::FILE) {
        const uint64_t size = file.fileSize();
        info.sizeBytes = size <= UINT32_MAX ? static_cast<uint32_t>(size) : 0;
    }
}

FLASHMEM void SDFileSystemBackend::fillEntry_(FsFile& file, interface::DirectoryEntry& entry) {
    const size_t nameLength = file.getName(entry.name, sizeof(entry.name));
    entry.nameTruncated = nameLength >= sizeof(entry.name);
    entry.type = typeOf_(file);
    entry.sizeBytes = 0;
    if (entry.type == interface::FileType::FILE) {
        const uint64_t size = file.fileSize();
        entry.sizeBytes = size <= UINT32_MAX ? static_cast<uint32_t>(size) : 0;
    }
}

FLASHMEM oc::type::Result<void> SDFileSystemBackend::removePath_(
    const char* path,
    bool recursive,
    uint8_t depth
) {
    FsFile file = SD.sdfs.open(path, O_RDONLY);
    if (!file.isOpen()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_NOT_FOUND, "path not found"}
        );
    }

    const bool isDirectory = file.isDir();
    if (!isDirectory) {
        file.close();
        if (!SD.sdfs.remove(path)) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "remove file failed"}
            );
        }
        return oc::type::Result<void>::ok();
    }

    if (!recursive) {
        file.close();
        if (!SD.sdfs.rmdir(path)) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "remove directory failed"}
            );
        }
        return oc::type::Result<void>::ok();
    }

    if (depth >= MAX_REMOVE_DEPTH) {
        file.close();
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "remove recursion depth exceeded"}
        );
    }

    file.close();

    while (true) {
        FsFile directory = SD.sdfs.open(path, O_RDONLY);
        if (!directory.isOpen() || !directory.isDir()) {
            if (directory.isOpen()) {
                directory.close();
            }
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_READ_FAILED, "open directory failed"}
            );
        }

        FsFile child;
        if (!child.openNext(&directory, O_RDONLY)) {
            directory.close();
            break;
        }

        char name[interface::FILESYSTEM_MAX_NAME_LENGTH] = {};
        const size_t nameLength = child.getName(name, sizeof(name));
        const bool nameTruncated = nameLength >= sizeof(name);
        child.close();
        directory.close();
        if (nameTruncated || name[0] == '\0') {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::INVALID_STATE, "child name unavailable"}
            );
        }

        char childPath[PATH_BUFFER_SIZE] = {};
        if (!joinPath_(path, name, childPath, sizeof(childPath))) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::INVALID_ARGUMENT, "child path too long"}
            );
        }

        auto childResult = removePath_(childPath, true, static_cast<uint8_t>(depth + 1));
        if (!childResult) {
            return childResult;
        }
    }

    if (!SD.sdfs.rmdir(path)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "remove directory failed"}
        );
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM bool SDFileSystemBackend::joinPath_(
    const char* parent,
    const char* name,
    char* out,
    size_t outSize
) {
    const size_t parentLength = std::strlen(parent);
    const size_t nameLength = std::strlen(name);
    const bool parentIsRoot = isRoot_(parent);
    const size_t separatorLength = parentIsRoot ? 0 : 1;
    const size_t totalLength = parentLength + separatorLength + nameLength;
    if (totalLength + 1 > outSize) {
        return false;
    }

    std::memcpy(out, parent, parentLength);
    size_t write = parentLength;
    if (!parentIsRoot) {
        out[write++] = '/';
    }
    std::memcpy(out + write, name, nameLength);
    out[write + nameLength] = '\0';
    return true;
}

FLASHMEM void SDFileSystemBackend::resetWriteStream_() {
    writeExpectedSize_ = 0;
    writeBytes_ = 0;
    writeActive_ = false;
}

}  // namespace oc::hal::teensy
