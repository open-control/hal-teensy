#include <SD.h>
#include <SDFileSystemBackend.hpp>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#undef interface
#else
#include <sys/mman.h>
#endif

TestSD SD;
namespace oc::hal::teensy::detail {
bool initializeBuiltInSD() { return true; }
bool reinitializeBuiltInSD() { return true; }
bool builtInSDMediaPresent() { return true; }
}

struct Disk : FsBlockDevice {
    std::map<uint32_t, std::array<uint8_t,512>> sectors;
    decltype(sectors) durable;
    int failRead = -1;
    int failWrite = -1;
    int failSync = -1;
    int syncs = 0;
    int reads = 0;
    int writes = 0;
    int readsBeforeWrite = -1;
    bool failed = false;
    bool persistent = false;
    uint32_t capacity = 131072;
    bool isBusy() override { return false; }
    uint32_t sectorCount() override { return capacity; }
    bool syncDevice() override {
        const int call = syncs++;
        if (call == failSync || (persistent && failSync >= 0 && call >= failSync)) {
            failed = true; return false;
        }
        durable = sectors;
        return true;
    }
    bool readSector(uint32_t sector, uint8_t* data) override {
        const int call = reads++;
        if (call == failRead || (persistent && failRead >= 0 && call >= failRead)) {
            failed = true; return false;
        }
        assert(sector < sectorCount());
        const auto it = sectors.find(sector);
        if (it == sectors.end()) std::memset(data, 0, 512);
        else std::memcpy(data, it->second.data(), 512);
        return true;
    }
    bool readSectors(uint32_t sector, uint8_t* data, size_t count) override {
        for (size_t i=0; i<count; ++i) if (!readSector(sector+i,data+i*512)) return false;
        return true;
    }
    bool writeSector(uint32_t sector, const uint8_t* data) override {
        const int call = writes++;
        if (call == 0) readsBeforeWrite = reads;
        if (call == failWrite || (persistent && failWrite >= 0 && call >= failWrite)) {
            failed = true; return false;
        }
        assert(sector < sectorCount());
        std::memcpy(sectors[sector].data(),data,512); return true;
    }
    bool writeSectors(uint32_t sector, const uint8_t* data, size_t count) override {
        for (size_t i=0; i<count; ++i) if (!writeSector(sector+i,data+i*512)) return false;
        return true;
    }
    void arm(int read) {
        reads=0; writes=0; syncs=0; readsBeforeWrite=-1; failed=false;
        failRead=read; failWrite=-1; failSync=-1;
    }
};

using oc::hal::teensy::SDFileSystemBackend;
using oc::type::ErrorCode;

int main() {
    int masked = 0;
    int injected = 0;
    int recovered = 0;
    int durabilityCuts = 0;
    void* desired = reinterpret_cast<void*>(uintptr_t{0x70000000});
#ifdef _WIN32
    auto* psram = static_cast<uint8_t*>(VirtualAlloc(desired,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
    auto* psram = static_cast<uint8_t*>(mmap(desired,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0));
#endif
    assert(psram == desired);
    for (int format : {16,32,64}) {
        const bool exfat = format == 64;
        Disk disk;
        disk.capacity = format == 16 ? 131072 : format == 32 ? 0x800000 : 0x100000;
        std::cerr << "format " << format << '\n';
        uint8_t sector[512];
        assert(exfat ? ExFatFormatter().format(&disk,sector) : FatFormatter().format(&disk,sector));
        assert(SD.sdfs.begin(&disk));
        SDFileSystemBackend fs;
        assert(fs.init());
        assert(fs.createDirectory("/one/two"));
        std::array<uint8_t,1536> data{};
        for (size_t i=0;i<data.size();++i) data[i]=static_cast<uint8_t>(i*17);
        assert(fs.write("/one/two/data.bin",0,data.data(),data.size()));
        assert(fs.stat("/one/two/data.bin").value().sizeBytes == data.size());
        for (int i=0;i<40;++i) {
            const auto path = "/one/two/long-file-name-" + std::to_string(i) + ".bin";
            auto seeded = fs.write(path.c_str(),0,data.data(),1);
            if (!seeded) std::cerr << path << " " << seeded.error().context << '\n';
            assert(seeded);
        }
        const auto pristine = disk.sectors;
        auto remount = [&] {
            disk.arm(-1);
            SD.sdfs.end();
            disk.sectors = pristine;
            disk.durable = pristine;
            assert(SD.sdfs.begin(&disk));
        };
        auto probe = [&](int operation) {
            if (operation == 0) {
                auto result = fs.stat("/one/two/data.bin");
                return result ? ErrorCode::OK : result.error().code;
            }
            if (operation == 1 || operation == 3) {
                std::array<uint8_t,1536> output{};
                auto* buffer = operation == 3 ? psram : output.data();
                auto result = fs.read("/one/two/data.bin",0,buffer,output.size());
                if (result && !disk.failed) assert(result.value() == output.size()
                    && std::memcmp(buffer,data.data(),data.size()) == 0);
                return result ? ErrorCode::OK : result.error().code;
            }
            int count = 0;
            auto result = fs.list("/one/two", [](const auto& entry, void* ctx) {
                ++*static_cast<int*>(ctx);
                return true;
            }, &count);
            if (result && !disk.failed) assert(count == 41);
            return result ? ErrorCode::OK : result.error().code;
        };
        for (int operation=0;operation<4;++operation) {
            remount(); disk.arm(-1);
            assert(probe(operation) == ErrorCode::OK);
            const int calls = disk.reads;
            for (bool persistent : {false,true}) for (int cut=0;cut<calls;++cut) {
                remount(); disk.arm(cut);
                disk.persistent = persistent;
                const auto result = probe(operation);
                assert(disk.failed);
                ++injected;
                assert(disk.reads < 128);
                assert(disk.writes == 0 && disk.sectors == pristine);
                if (result != ErrorCode::STORAGE_READ_FAILED) {
                    std::cout << "MASKED format=" << format << " op=" << operation
                              << " sector-call=" << cut << " error=" << int(result) << '\n';
                    ++masked;
                }
            }
            std::cout << "format=" << format << " op=" << operation << " cuts=" << calls << '\n';
        }
        auto mutate = [&](int operation) {
            auto error = [](const auto& result) { return result ? ErrorCode::OK : result.error().code; };
            switch (operation) {
                case 0: return error(fs.createDirectory("/one/two"));
                case 1: return error(fs.write("/one/two/data.bin",0,data.data(),data.size()));
                case 2: return error(fs.beginWrite("/one/two/data.bin",data.size()));
                case 3: return error(fs.rename("/one/two/data.bin","/one/two/moved.bin"));
                case 4: return error(fs.remove("/one/two/data.bin"));
                default: return error(fs.flush("/one/two/data.bin"));
            }
        };
        for (int operation=0;operation<6;++operation) {
            remount(); disk.arm(-1);
            assert(mutate(operation) == ErrorCode::OK);
            const int preflight = disk.readsBeforeWrite < 0 ? disk.reads : disk.readsBeforeWrite;
            const auto committed = disk.sectors;
            fs.abortWrite();
            for (bool persistent : {false,true}) for (int cut=0;cut<preflight;++cut) {
                remount(); disk.arm(cut); disk.persistent=persistent;
                const auto result = mutate(operation);
                assert(disk.failed);
                ++injected;
                // The destination really is absent. A fresh, successful negative
                // lookup may recover a transient failure and authorize this rename.
                if (operation == 3 && !persistent && result == ErrorCode::OK) {
                    assert(disk.sectors == committed);
                    ++recovered;
                    continue;
                }
                if (result == ErrorCode::OK || result == ErrorCode::RESOURCE_NOT_FOUND) {
                    std::cout << "MASKED mutation=" << operation << " format=" << format
                              << " cut=" << cut << " error=" << int(result) << '\n';
                    ++masked;
                }
                // Errors in preflight must not authorize a write or delete.
                if (disk.writes != 0 || disk.sectors != pristine) {
                    std::cout << "MUTATED after failed preflight op=" << operation
                              << " format=" << format << " cut=" << cut << '\n';
                    ++masked;
                }
                fs.abortWrite();
                assert(disk.sectors == pristine);
            }
            std::cout << "format=" << format << " mutation=" << operation << " cuts=" << preflight << '\n';
        }
        remount();
        for (const char* missing : {"/absent", "/one/absent/file", "/one/two/absent.bin"}) {
            const auto info = fs.stat(missing);
            assert(!info && info.error().code == ErrorCode::RESOURCE_NOT_FOUND);
        }
        int visited = 0;
        assert(fs.list("/one/two",[](const auto&,void* ctx) { ++*static_cast<int*>(ctx); return false; }, &visited));
        assert(visited == 1); // Intentional visitor stop is not a failed enumeration.
        std::array<uint8_t,1536> tail;
        tail.fill(0xCC);
        const auto eof = fs.read("/one/two/data.bin",1500,tail.data(),tail.size());
        assert(eof && eof.value() == 36 && tail[36] == 0xCC);
        assert(std::memcmp(tail.data(),data.data()+1500,36) == 0);
        assert(fs.read("/one/two/data.bin",1536,tail.data(),tail.size()).value() == 0);
        assert(fs.read("/one/two/data.bin",0,nullptr,0).value() == 0);
        std::memset(psram,0xCC,1536);
        const auto stagedEof = fs.read("/one/two/data.bin",1500,psram,1536);
        assert(stagedEof && stagedEof.value() == 36 && psram[36] == 0xCC);
        assert(std::memcmp(psram,data.data()+1500,36) == 0);
        assert(fs.createDirectory("/new/deep"));
        assert(fs.rename("/one/two/data.bin","/new/deep/moved.bin"));
        assert(fs.beginWrite("/new/deep/stream.bin",data.size()));
        assert(fs.appendWrite(data.data(),data.size()));
        assert(fs.finishWrite());
        assert(fs.flush("/new/deep/stream.bin"));
        assert(fs.remove("/new",oc::interface::RemoveMode::RECURSIVE));
        // A successful result must survive loss of all device and filesystem caches.
        // Verify the durable image before abort/destructors can retry a failed sync.
        std::array<uint8_t,1536> replacement;
        replacement.fill(0xA7);
        for (int operation=0; operation<7; ++operation) {
            const bool stream = operation >= 3;
            const size_t length = operation == 3 ? 0 : operation == 4 ? 1
                : operation == 5 ? 513 : replacement.size();
            const char* path = operation == 1 ? "/one/two/new.bin" : "/one/two/data.bin";
            auto prepare = [&] {
                remount(); disk.persistent=false;
                if (stream) {
                    assert(fs.beginWrite(path,length));
                    std::memcpy(psram,replacement.data(),length);
                    assert(fs.appendWrite(psram,length));
                }
                disk.arm(-1);
            };
            auto perform = [&] {
                if (stream) return bool(fs.finishWrite());
                if (operation == 2) return bool(fs.flush(path));
                return bool(fs.write(path,0,replacement.data(),length));
            };
            auto verifyReboot = [&] {
                const auto persisted = disk.durable;
                disk.arm(-1); disk.persistent=false;
                fs.abortWrite(); SD.sdfs.end();
                disk.sectors = persisted;
                assert(SD.sdfs.begin(&disk));
                std::array<uint8_t,1536> output{};
                const auto read = fs.read(path,0,output.data(),output.size());
                const auto& expected = operation == 2 ? data : replacement;
                assert(fs.stat(path).value().sizeBytes == length);
                assert(read && read.value() == length);
                assert(std::memcmp(output.data(),expected.data(),length) == 0);
            };
            prepare(); assert(perform());
            const int syncCalls = disk.syncs, writeCalls = disk.writes;
            const auto expectedImage = disk.durable;
            verifyReboot();
            std::cout << "durability format=" << format << " op=" << operation
                      << " writes=" << writeCalls << " syncs=" << syncCalls << '\n';
            for (bool sync : {false,true}) for (bool persistent : {false,true}) {
                for (int cut=0; cut<(sync ? syncCalls : writeCalls); ++cut) {
                    prepare(); disk.persistent=persistent;
                    if (sync) disk.failSync=cut; else disk.failWrite=cut;
                    const bool success = perform();
                    assert(disk.failed);
                    ++durabilityCuts;
                    // Even a clean close's last failed durability boundary must surface.
                    if (success && ((sync && cut == syncCalls-1) || disk.durable != expectedImage)) {
                        std::cout << "MASKED durability format=" << format << " op=" << operation
                                  << " sync=" << sync << " cut=" << cut
                                  << " persistent=" << persistent << '\n';
                        ++masked;
                    }
                    if (stream) assert(!fs.finishWrite()); // Success and failure both end the session.
                    if (success && disk.durable == expectedImage) verifyReboot();
                    disk.arm(-1); disk.persistent=false; fs.abortWrite();
                }
            }
        }
        SD.sdfs.end();
    }
#ifdef _WIN32
    assert(VirtualFree(psram,0,MEM_RELEASE));
#else
    assert(munmap(psram,4096) == 0);
#endif
    std::cout << "injected failures=" << injected << " verified recoveries=" << recovered
              << " durability cuts=" << durabilityCuts
              << " masked failures=" << masked << '\n';
    return masked ? 1 : 0;
}
