#pragma once
#include <FsLib/FsLib.h>
struct TestVolume : FsVolume {
    uint8_t sdErrorCode() const { return 0; }
    uint32_t sdErrorData() const { return 0; }
};
struct TestSD { TestVolume sdfs; };
extern TestSD SD;
