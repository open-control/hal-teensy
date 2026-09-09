# Teensy SDIO command/transfer ownership

Run with Python 3 and a native C++17 compiler:

```sh
python test/sdio_driver/run.py /path/to/SdFat/src/SdCard/SdioTeensy.cpp --cxx c++ --output /tmp/sdio-test
```

The test verifies the pinned source, applies the production build adapter and
extracts eight function bodies from both original and corrected drivers. It
compiles each against the same model of write-one-to-clear IRQ registers. It
also checks that an unreviewed SDK source fails before compilation. Generated
sources, execution logs and normalized source hashes remain in the output folder.

The model schedules two competing orders:

1. The DMA ISR consumes Command Complete before foreground polling begins.
2. Foreground command acknowledgement happens before the DMA ISR consumes
   Transfer Complete.

The original driver falsely times out in both cases. The corrected driver must
finish, while still rejecting absent completion, stale cached/hardware status,
command errors and transfer errors. A caller that already masked interrupts must
leave with that mask intact. ARM's MRS instruction is replaced only by a model
getter; the remaining extracted driver statements are unchanged.

The numerical clock is virtual, advanced by the model. This is a regression test
of interrupt ordering, not a CPU benchmark, ARM emulator or physical SD-card test.
Hardware qualification must additionally validate normal operation and firmware
performance. The adapter changes no timeout duration, adds no retry or buffer,
and never waits with interrupts disabled.
