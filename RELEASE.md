# Changelog for Code Example for streaming the on-chip range FFT of the XENSIV&trade; BGT60CUTRxx Radar Sensor

## Overview

This code example streams the output of the on-chip range FFT of the XENSIV&trade; BGT60CUTRxx Radar Sensor to a host over SEGGER Real Time Transfer (RTT).

## What's included?

- Source code for the range FFT streaming example application.
- Driver for the XENSIV&trade; BGT60CUTRxx Radar Sensor.

## What's Changed?

### 1.1.7

#### Add
- Radar frames are streamed to the host over SEGGER RTT and described in the `rtt` block of the `get_info` response, instead of being printed as floating point text over the UART
- `DSP_STAGES` selects which outputs of the sensor's processing chain are streamed, without changing the register configuration: the averaged ADC samples, the range FFT, the Doppler FFT, or a combination the sensor forwards as separate sections
- The `rtt` block reports a `dfft` section length alongside `adc` and `rfft`
- The sensor's configured FFT sizes are checked against the acquisition parameters at start-up, because the host derives the range and Doppler bin counts from them
- Radar frames larger than the FIFO are assembled from several threshold interrupts, and a partial frame is discarded after a FIFO error so the stream cannot stay misaligned

#### Change
- Fixed-point to floating point conversion, the 3D output buffer and the UART capture script were removed; the firmware forwards the FIFO words verbatim and the host decodes them
- The range Doppler sequence is started through the sensor's motion detection timer rather than sequence 0

#### Remove
- `BGT60CUTR13B_Configuration_RD_16Chirps_RFFT` and `ULP_10HZ_RD_10HZ_Function_corr_20260529_153816` radar configurations

### 0.1.0

This is the initial release of the code example for streaming the on-chip range FFT of the XENSIV&trade; BGT60CUTRxx Radar Sensor.

#### Add
- Initializes the board, peripherals, and debug UART.
- Configures GPIOs and the SPI interface for radar sensor communication.
- Initializes the radar sensor and sets up the interrupt handler for fifo data ready events.
- Sets up the fifo read configuration.
- Reads the content of the fifo and stores the content internally in a buffer.
- turns on the LED when new fifo data is available and reading is in progress.
- turns off the LED when fifo data reading is completed.


#### Change

#### Fixed

#### Improved

## Supported software and tools

This version of the XENSIV&trade; BGT60CUTRxx 60GHz radar sensor code example was validated for compatibility with the following software and tools:

| Software and tools                        | Version |
| :---                                      | :----:  |
| ModusToolbox&trade; software environment        | 3.1.0   |
| GCC compiler                              | 10.3.1  |

Minimum required ModusToolbox&trade; software environment: v3.2

## More information

Use the following links for more information, as needed:


© Infineon Technologies AG, 2021.
