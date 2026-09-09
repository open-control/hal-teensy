# SD filesystem error contract

This standalone native target compiles the real `SDFileSystemBackend.cpp` and the installed Teensy SDK's FAT16/FAT32/exFAT code. A sparse memory block device injects transient and persistent failures at every sector-read boundary of the tested operations. No card is formatted or written by these native tests.

Configure with CMake and Ninja, setting `OC_SDFAT_SOURCE_DIR` to the installed `framework-arduinoteensy/libraries/SdFat/src`, then build and run CTest. Example from the HAL repository:

```text
cmake -S test/sd_filesystem -B build/sd-filesystem -G Ninja -DOC_SDFAT_SOURCE_DIR=<SDK>/libraries/SdFat/src -DCMAKE_BUILD_TYPE=Debug
cmake --build build/sd-filesystem
ctest --test-dir build/sd-filesystem --output-on-failure
```

`OC_HAL_SD_SOURCE_DIR` can point to a saved backend source directory to reproduce the pre-fix failures with the same tests. Assertions remain enabled in every build type. The test reserves 4096 bytes at `0x70000000` on Windows or Linux so the production PSRAM staging branch is exercised; inability to reserve this address fails the test rather than silently skipping it.

The SDK copy under the build directory disables Arduino print/string features, supplies the host clock/flash-string declaration, and selects the generic block-device interface without including the hardware SPI driver. Filesystem algorithms are unchanged; the installed SDK remains untouched. `SD.h` only supplies the mounted real FsVolume and unused diagnostic accessors. Media availability is fixed true: SDIO timing, card-presence probes and physical power cuts require separate hardware qualification.

Coverage includes clean EOF, absent paths at multiple depths, deliberate visitor stop, complete direct and staged reads, failed directory entries, failed lookup before mutations, and normal create/stream/flush/rename/remove. For a transient error while probing a truly absent rename destination, success is permitted only if the resulting entire disk image exactly matches the successful reference. All other failed mutation preflights must leave the disk image unchanged. The suite verifies surfaced I/O failures; it does not claim comprehensive detection of malformed FAT directory metadata.

Durability coverage injects transient and persistent failures at every write-sector and device-sync boundary for existing/new writes, flush, and staged stream finalization of 0, 1, 513 and 1536 bytes. The sparse device retains a separate durable image only after successful sync. A successful API result must match the successful reference image before cleanup can retry anything, and remounting that image must recover the exact size and bytes. A failed final close/sync must be reported even if a previous sync had already persisted the bytes; this checks error reporting as well as durability. Finalization always ends the session. This model tests the software contract, not physical SD-controller power-loss guarantees.
