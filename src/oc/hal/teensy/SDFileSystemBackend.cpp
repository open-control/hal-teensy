#include "SDFileSystemBackend.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

namespace oc::hal::teensy {

FLASHMEM oc::type::Result<void> SDFileSystemBackend::init() {
    if (initialized_) {
        return oc::type::Result<void>::ok();
    }
    if (!SD.begin(BUILTIN_SDCARD)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::HARDWARE_INIT_FAILED, "SD.begin failed"}
        );
    }

    initialized_ = true;
    return oc::type::Result<void>::ok();
}

FLASHMEM bool SDFileSystemBackend::available() const {
    return initialized_ && SD.mediaPresent();
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

    const size_t written = file.write(data, size);
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

FLASHMEM oc::type::Result<void> SDFileSystemBackend::ensureAvailable_() const {
    if (!initialized_) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "filesystem not initialized"}
        );
    }
    if (!SD.mediaPresent()) {
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

}  // namespace oc::hal::teensy
