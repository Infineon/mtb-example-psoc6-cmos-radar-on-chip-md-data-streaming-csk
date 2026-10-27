RTT
===

SEGGER's Real Time Transfer (RTT) is a technology for interactive user I/O in embedded applications.

## Overview

It allows to send and receive data via a debug probe to and from the target device. The debug probe could be connected
via SWD, JTAG, or other debug interfaces. RTT is implemented in the target application as a small buffer in RAM and a
few functions to read and write data to this buffer. The debug probe reads and writes data to this buffer as well. Read
data is sent to the host via a TCP socket, and write data is received from the host via the same TCP socket ( this TCP
socket creation is done by OpenOCD).

The following figure illustrates the data flow from the application to the host and vice versa: ::

    ┌─────────────────┐
    │   Application   │
    └────────┬────────┘
             │
    ┌────────┴────────┐
    │ TCP socket      │
    │ (OpenOCD)       │
    └────────┬────────┘
             │
    ┌────────┴────────┐
    │  Debug probe    │
    └────────┬────────┘
             │
    ┌────────┴────────┐
    │      MCU        │
    └─────────────────┘

For details see: https://kb.segger.com/RTT

## Performance measurements

### Summary

- At maximum ca. 1 Megabit/s (Mbps) (ca. 130 KB/s) could be achieved with KitProg3 on-chip debugger (OCD) with 16 MHz
  SWD speed
- If more than ca. 1 Megabit/s  is required an external J-Link debugger with 16 MHz SWD speed is necessary.

### Details

A working student has conducted different measurements which are not fully reproducible with the CSK KitProg3 OCD.

You can see the performance of RTT communication with 1ms poll interval for 2Mbps target speed for 6539 byte radar
frame. Each test ran for 20 seconds.

| Buffer Size | Clock Frequency | Max (Mbps) | Min (Mbps) | Avg (Mbps) | Range (Max−Min) (Mbps) |
|---|---|---:|---:|---:|---:|
| 64 KB | 4 MHz | 1.32 | 1.24 | 1.29 | 0.08 |
| 64 KB | 8 MHz | 1.80 | 1.23 | 1.71 | 0.57 |
| 64 KB | 16 MHz | 1.99 | 1.35 | 1.78 | 0.64 |
| 128 KB | 4 MHz | 1.32 | 1.22 | 1.28 | 0.10 |
| 128 KB | 8 MHz | 1.80 | 1.68 | 1.74 | 0.12 |
| 128 KB | 16 MHz | 1.96 | 1.50 | 1.81 | 0.46 |
| 256 KB | 4 MHz | 1.31 | 1.26 | 1.29 | 0.05 |
| 256 KB | 8 MHz | 1.79 | 1.63 | 1.73 | 0.16 |
| 256 KB | 16 MHz | 2.07 | 1.66 | 1.89 | 0.41 |

## Included SEGGER RTT Sources

  * `RTT/`
    * `SEGGER_RTT.c`               - Main module for RTT.
    * `SEGGER_RTT.h`               - Main header for RTT.
    * `SEGGER_RTT_ASM_ARMv7M.S`    - Assembly-optimized implementation of RTT functions for ARMv7M processors.
    * `SEGGER_RTT_Printf.c`        - Simple implementation of printf (`SEGGER_RTT_Printf()`) to write formatted strings via RTT.
  * `Syscalls/`
    * `SEGGER_RTT_Syscalls_*.c`    - Low-level syscalls to retarget `printf()` to RTT with different toolchains.
  * `Config/`
    * `SEGGER_RTT_Conf.h`          - RTT configuration file.
  * `Examples/`
    * `Main_RTT_InputEchoApp.c`    - Example application which echoes input on Channel 0.
    * `Main_RTT_MenuApp.c`         - Example application to demonstrate RTT bi-directional functionality.
    * `Main_RTT_PrintfTest.c`      - Example application to test RTT's simple printf implementation.
    * `Main_RTT_SpeedTestApp.c`    - Example application to measure RTT performance. (Requires embOS)
