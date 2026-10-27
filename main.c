/*
 * main.c
 *
 * Description: Streams the on-chip range FFT of the XENSIV BGT60CUTRxx sensor to a host over
 *              SEGGER RTT, reading it from the sensor's FIFO buffer.
 *
 * Related Document: See README.md
 *
 ********************************************************************************
 * Copyright (c) (2016-2022), Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/

#include "cyhal.h"
#include "cy_retarget_io.h"
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#include "common.h"
#include "dsp_chain.h"
#include "frame_assembler.h"
#include "protocol.h"
#include "resource_map.h"
#include "radar_config/BGT60CUTR13B_Configuration_RD_32Chirps.h"
#include "radar_fifo_read.h"
#include "rtt_stream.h"
#include "version.h"
#include "xensiv_bgt60cutrxx_mtb.h"
#include "xensiv_bgt60cutrxx_regs.h"

/*******************************************************************************
* Macros
*******************************************************************************/

/** baud rate for the UART communication
 * The baudrate must be high enough to transmit the data before the next interrupt occurs.
 */
#define UART_BAUDRATE 2000000

/** Default SPI speed for the XENSIV BGT60CUTRxx sensor, which is used during startup */
#define SPI_FREQUENCY               (18750000UL) // 18.75MHz

/** Stages of the sensor's processing chain that are streamed to the host.
 *
 * Every selection runs the same register configuration and differs only in what
 * MD_RD_FIFO_CTRL forwards, so switching here is enough to compare an FFT output against the
 * input it was computed from. Combine the DSP_STAGE_* flags of dsp_chain.h with a bitwise OR. */
#define DSP_STAGES (DSP_STAGE_RFFT)

#if DSP_CHAIN_STAGES_INTERLEAVE(DSP_STAGES)
#error \
    "This firmware does not support deinterleaving of the ADC and range FFT stages. Please, select only one of them."
#endif

/** The shipped sequence transmits on a single antenna; the register configuration does not name it. */
#define NUM_TX_ANTENNAS (1U)

/** FIFO words of one radar frame, all selected sections together. */
#define NUM_FRAME_WORDS \
    DSP_CHAIN_FRAME_WORDS(DSP_STAGES, RD_RX_ANTENNAS, RD_NUMBER_OF_CHIRPS, RD_SAMPLES_PER_CHIRP)

/** FIFO threshold IRQ fires one word past the threshold. */
#define NUM_THRESHOLD_WORDS (FIFO_THRESHOLD + 1U)

/** Threshold interrupts that deliver one whole radar frame. */
#define NUM_SLICES_PER_FRAME (NUM_FRAME_WORDS / NUM_THRESHOLD_WORDS)

/** One FIFO word is 32 bit wide. I.e., 16-bit ADC samples share one FIFO word. Required by radar_fifo_read(). */
#define FIFO_WORDS_TO_ADC_SAMPLES(words) ((words) * 2U)

/** Up-buffer sized so a briefly busy host costs nothing; the wider the selection, the fewer
 * whole frames it absorbs. */
#define RTT_BUFFER_SIZE (131072U)

// Enable this newlib workaround only for GCC: it flushes protocol lines promptly. IAR lacks
// these stdio identifiers, so disabling it there keeps the build valid but uses default buffering.
#if defined(__GNUC__) && !defined(__ARMCC_VERSION) && !defined(__IAR_SYSTEMS_ICC__)
/** Holds the longest protocol line (the get_info reply with the rtt block, ~560 bytes) so it is
 * written in one go. */
#define STDOUT_BUFFER_SIZE (1024U)
#endif

/** Dropped frames are reported once, then once per this many further drops.
 *
 * Nothing drains the RTT up-buffer until a host attaches to the debug probe, so an unattended
 * board drops every frame at the acquisition rate. One line per drop keeps the UART busy enough
 * that incoming commands are missed and the get_info reply never gets out. */
#define FRAMES_DROPPED_REPORT_INTERVAL (128U)

/** Set to 1 to report the heartbeat and the sensor status registers.
 *
 * Diagnostic output only: it costs one UART line and four SPI register reads per interval. */
#define PRINT_HEART_BEAT_DEBUG_INFO (0)

/** Period of the heartbeat line, in seconds.
 *
 * The heartbeat is driven by the LPTimer rather than by radar activity, so it keeps reporting
 * when the sensor stops delivering interrupts. That distinguishes a stalled sensor from a
 * blocked or flooded application, which look identical from the host side. */
#define HEARTBEAT_INTERVAL_S (2U)

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static void xensiv_bgt60cutrxx_irq_handler(void* callback_arg, cyhal_gpio_event_t event);
static int32_t xensiv_bgt60cutrxx_board_init(void);
static int32_t initialize_board_leds(void);
static void report_frames_dropped(uint32_t frames_dropped);
#if PRINT_HEART_BEAT_DEBUG_INFO
static void report_sensor_status(void);
#endif
static void clear_fifo(uint32_t forwarding_mask);

/*******************************************************************************
* Global Variables
*******************************************************************************/

/** Radar sensor object */
static xensiv_bgt60cutrxx_mtb_t sensor;

/**
 * @brief Data buffer used to store one complete radar frame.
 *
 * Addressed in 16-bit units because that is what the FIFO read and the RTT stream take; the
 * payload itself is a sequence of 32-bit device words.
 */
static uint16_t frame_data_buffer[FIFO_WORDS_TO_ADC_SAMPLES(NUM_FRAME_WORDS)];

/** Tracks which slice of the current radar frame the next threshold interrupt fills */
static frame_assembler_t frame_assembler;

/** Helper variable used by IRQ handler to indicate data events to main loop */
static volatile bool radar_event_irq = false;

#if defined(__GNUC__) && !defined(__ARMCC_VERSION) && !defined(__IAR_SYSTEMS_ICC__)
/** Line buffer for stdout, supplied statically to keep stdio off the heap */
static char stdout_buffer[STDOUT_BUFFER_SIZE];
#endif

/** Up-buffer memory for the RTT stream, owned by SEGGER RTT from rtt_stream_init() onwards */
static uint8_t rtt_stream_buffer[RTT_BUFFER_SIZE];

/** Time source for the heartbeat */
#if PRINT_HEART_BEAT_DEBUG_INFO
static cyhal_lptimer_t heartbeat_timer;
#endif

/** Description of the RTT stream; the protocol handler keeps a reference to it */
static protocol_rtt_info_t rtt_info =
{
    .radar_config              =
    {
        .num_tx                = NUM_TX_ANTENNAS,
        .num_rx                = RD_RX_ANTENNAS,
        .num_chirps            = RD_NUMBER_OF_CHIRPS,
        .num_samples_per_chirp = RD_SAMPLES_PER_CHIRP,
        .range_resolution_m    = RD_RANGE_RESOLUTION_M
    }
};

/** Platform interface configuration */
static xensiv_bgt60cutrxx_mtb_iface_config_t mcu_iface_config =
{
    .selpin       = PIN_XENSIV_BGT60CUTRXX_SPI_CSN,
    .rstpin       = PIN_XENSIV_BGT60CUTRXX_RSTN,
    .irqpin       = PIN_XENSIV_BGT60CUTRXX_IRQ,
    .irq_priority = CYHAL_ISR_PRIORITY_DEFAULT,
    .spi_mosi     = CYBSP_SPI_MOSI,
    .spi_miso     = CYBSP_SPI_MISO,
    .spi_clk      = CYBSP_SPI_CLK,
    .spi_speed_hz = SPI_FREQUENCY,
};

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/**
 * @brief Interrupt handler for the XENSIV BGT60CUTRxx radar sensor.
 *
 * This function is called when a radar interrupt occurs. It sets the radar_event_irq
 * flag to signal the main loop that radar data is ready for processing.
 *
 * @param[in] callback_arg User-defined callback argument (unused).
 * @param[in] event GPIO event that triggered the interrupt.
 */
static void xensiv_bgt60cutrxx_irq_handler(void* callback_arg, cyhal_gpio_event_t event)
{
    (void)callback_arg;
    (void)event;

    // flag to indicate radar event
    radar_event_irq = true;
}


/** Helper function to initialize board specific settings */
static int32_t xensiv_bgt60cutrxx_board_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Enable LDO */
    result |= cyhal_gpio_init(PIN_XENSIV_BGT60CUTRXX_LDO_EN, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);

    /* Set specific pins to disable BGT60CUTRxx bootloader */
    result |= cyhal_gpio_init(PIN_XENSIV_BGT60CUTRXX_DIO2, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);
    result |= cyhal_gpio_init(PIN_XENSIV_BGT60CUTRXX_DIO3, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);

    /* Set level shifter direction */
    result |= cyhal_gpio_init(SHIELD_XENSIV_DIR4_EN, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);

    if (result != CY_RSLT_SUCCESS)
    {
        return -1;
    }

    return XENSIV_BGT60CUTRXX_STATUS_OK;
}


/** Helper function to initialize board LEDs */
static int32_t initialize_board_leds(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    result |= cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT,
                              CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);
    result |= cyhal_gpio_init(SHIELD_XENSIV_RADAR_LED_RED, CYHAL_GPIO_DIR_OUTPUT,
                              CYHAL_GPIO_DRIVE_STRONG, SHIELD_XENSIV_RADAR_LED_RED_OFF);
    result |= cyhal_gpio_init(SHIELD_XENSIV_RADAR_LED_GREEN, CYHAL_GPIO_DIR_OUTPUT,
                              CYHAL_GPIO_DRIVE_STRONG, SHIELD_XENSIV_RADAR_LED_GREEN_OFF);

    if (result != CY_RSLT_SUCCESS)
    {
        return -1;
    }

    return XENSIV_BGT60CUTRXX_STATUS_OK;
}


/**
 * @brief Reports lost radar frames to the host, coalesced to keep the control link usable.
 *
 * The first drop is reported immediately so frame loss is never silent, after which the report
 * repeats every FRAMES_DROPPED_REPORT_INTERVAL drops. The reported count is always the
 * cumulative total, so no drop is hidden, only the line that would have announced it.
 *
 * @param frames_dropped Cumulative number of radar frames that could not be delivered.
 */
static void report_frames_dropped(uint32_t frames_dropped)
{
    if ((frames_dropped == 1U) || ((frames_dropped % FRAMES_DROPPED_REPORT_INTERVAL) == 0U))
    {
        protocol_print_frames_dropped(frames_dropped);
    }
}


#if PRINT_HEART_BEAT_DEBUG_INFO

/**
 * @brief Reports the sensor state that explains why acquisition stopped.
 *
 * Read from the heartbeat rather than from the interrupt path, because the interesting case is
 * the one where the sensor has stopped raising interrupts and nothing else would be read again.
 */
static void report_sensor_status(void)
{
    uint32_t ir_stat = 0;
    uint32_t fifo_stat = 0;
    uint32_t rd_stat = 0;
    uint32_t gtmr_ctrl = 0;

    (void)xensiv_bgt60cutrxx_get_reg(&sensor.dev, BGT60CUTR13B_REG_IR_STAT, &ir_stat);
    (void)xensiv_bgt60cutrxx_get_reg(&sensor.dev, BGT60CUTR13B_REG_FIFO_STAT, &fifo_stat);
    (void)xensiv_bgt60cutrxx_get_reg(&sensor.dev, BGT60CUTR13B_REG_MD_RD_STAT, &rd_stat);
    (void)xensiv_bgt60cutrxx_get_reg(&sensor.dev, BGT60CUTR13B_REG_GTMR0_CTRL, &gtmr_ctrl);

    protocol_print_info("Sensor: IR_STAT 0x%08" PRIX32 ", FIFO_STAT 0x%08" PRIX32
                        ", MD_RD_STAT 0x%08" PRIX32 ", GTMR0_CTRL 0x%08" PRIX32,
                        ir_stat, fifo_stat, rd_stat, gtmr_ctrl);
}


#endif // if PRINT_HEART_BEAT_DEBUG_INFO


/**
 * @brief Discards the FIFO contents so the next radar frame starts on a frame boundary.
 *
 * FIFOCLR_EN is a level, so it is pulsed by restoring the forwarding selection afterwards.
 *
 * @param forwarding_mask Stage selection to restore, as written to MD_RD_FIFO_CTRL.
 */
static void clear_fifo(uint32_t forwarding_mask)
{
    (void)xensiv_bgt60cutrxx_set_reg(&sensor.dev, BGT60CUTR13B_REG_MD_RD_FIFO_CTRL,
                                     forwarding_mask | BGT60CUTR13B_MD_RD_FIFO_CTRL_FIFOCLR_EN_msk);
    (void)xensiv_bgt60cutrxx_set_reg(&sensor.dev, BGT60CUTR13B_REG_MD_RD_FIFO_CTRL, forwarding_mask);
}


/**
 * @brief Main application entry point for streaming the on-chip range FFT.
 *
 * This function performs the following operations:
 * - Initializes the device, board peripherals, and UART for debug output
 * - Configures GPIO pins and SPI interface for the XENSIV BGT60CUTRxx radar sensor
 * - Selects which stage of the sensor's processing chain is forwarded to the FIFO
 * - Verifies that a whole number of FIFO threshold interrupts delivers one radar frame
 * - Enters an infinite loop serving the host protocol, reading one slice of a radar frame per
 *   interrupt, and streaming each completed frame to the host
 *
 * @return int This function never returns under normal operation due to infinite loop.
 */
int main(void)
{
    cy_rslt_t result;

    #if defined (CY_DEVICE_SECURE)
    cyhal_wdt_t wdt_obj;

    /* Clear watchdog timer so that it doesn't trigger a reset */
    result = cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());
    CY_ASSERT(CY_RSLT_SUCCESS == result);
    cyhal_wdt_free(&wdt_obj);
    #endif /* #if defined (CY_DEVICE_SECURE) */

    // Initialize MCU and board peripherals
    result = cybsp_init();
    result |= initialize_board_leds();
    if (result != CY_RSLT_SUCCESS)
    {
        // MCU and board init failed. Stop program execution
        CY_ASSERT(0);
        return 1;
    }

    // Enable global interrupts
    __enable_irq();

    // Initialize UART port used for logging
    result = cy_retarget_io_init_fc(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, NC, NC, UART_BAUDRATE);
    if (result != CY_RSLT_SUCCESS)
    {
        // retarget-io init failed. Stop program execution
        CY_ASSERT(0);
        return 1;
    }

    // wait until UART is ready
    cyhal_system_delay_ms(5000);

    // Enable this newlib workaround only for GCC: it flushes protocol lines promptly. IAR lacks
    // these stdio identifiers, so disabling it there keeps the build valid but uses default buffering.
    #if defined(__GNUC__) && !defined(__ARMCC_VERSION) && !defined(__IAR_SYSTEMS_ICC__)
    // newlib leaves stdout fully buffered because the retarget-io backend has no fstat();
    // this example prints too rarely to ever fill that buffer, so its lines would never reach the host.
    // Every protocol message is newline terminated, so line buffering flushes each one while still
    // handing whole lines to the UART rather than one character per write.
    setvbuf(stdout, stdout_buffer, _IOLBF, sizeof(stdout_buffer));
    #endif

    protocol_print_info("***************************************************************");
    protocol_print_info("Radar: On-Chip Data Streaming via RTT Example (ADC, RFFT, DFFT)");
    protocol_print_info("***************************************************************");

    protocol_print_info("For more projects, "
                        "visit our code examples repositories:");

    protocol_print_info("https://github.com/Infineon/"
                        "Code-Examples-for-ModusToolbox-Software");

    result = xensiv_bgt60cutrxx_board_init();
    result |= xensiv_bgt60cutrxx_platform_open(&sensor.iface, &mcu_iface_config);
    if (XENSIV_BGT60CUTRXX_STATUS_OK != result)
    {
        protocol_print_error("Could not initialize the platform interfaces");
        return 1;
    }

    // Radar sensor initialization
    xensiv_bgt60cutrxx_power_mode_t power_mode = ACTIVE_MODE;
    bool deep_sleep_after_calibration = false;
    result = xensiv_bgt60cutrxx_platform_init(&sensor, &mcu_iface_config, register_list, ARRAY_SIZE(
                                                  register_list), power_mode, deep_sleep_after_calibration);
    if (XENSIV_BGT60CUTRXX_STATUS_OK != result)
    {
        protocol_print_error("Could not initialize the radar sensor");
        return 1;
    }
    protocol_print_info("Radar sensor is initialized");

    dsp_chain_t dsp_chain;
    dsp_chain_describe(DSP_STAGES, RD_RX_ANTENNAS, RD_NUMBER_OF_CHIRPS, RD_SAMPLES_PER_CHIRP, &dsp_chain);

    uint32_t md_rd_ctrl = 0;
    if (XENSIV_BGT60CUTRXX_STATUS_OK !=
        xensiv_bgt60cutrxx_get_reg(&sensor.dev, BGT60CUTR13B_REG_MD_RD_CTRL, &md_rd_ctrl))
    {
        protocol_print_error("Could not read the FFT sizes from the sensor");
        return 1;
    }

    if (!dsp_chain_fft_sizes_match(md_rd_ctrl, RD_NUMBER_OF_CHIRPS, RD_SAMPLES_PER_CHIRP))
    {
        protocol_print_error("MD_RD_CTRL 0x%08" PRIX32 " does not match %" PRIu32
                             " chirps of %" PRIu32 " samples",
                             md_rd_ctrl, (uint32_t)RD_NUMBER_OF_CHIRPS, (uint32_t)RD_SAMPLES_PER_CHIRP);
        return 1;
    }

    // overrides the forwarding the register configuration was generated for
    result = xensiv_bgt60cutrxx_set_reg(&sensor.dev, BGT60CUTR13B_REG_MD_RD_FIFO_CTRL, dsp_chain.forwarding_mask);
    if (XENSIV_BGT60CUTRXX_STATUS_OK != result)
    {
        protocol_print_error("Could not select the FIFO data path");
        return 1;
    }

    // The register configuration leaves IR0 in level mode, where the pin mirrors "FIFO level
    // above threshold". A stage that delivers a whole frame at once, such as the Doppler FFT,
    // leaves the FIFO above the threshold after the first slice, so the level never falls and
    // the rising-edge GPIO sees no further edge. Pulse mode emits one edge per event instead.
    uint32_t ir_ctrl = 0;
    result = xensiv_bgt60cutrxx_get_reg(&sensor.dev, BGT60CUTR13B_REG_IR_CTRL, &ir_ctrl);
    if (XENSIV_BGT60CUTRXX_STATUS_OK == result)
    {
        ir_ctrl |= BGT60CUTR13B_IR_CTRL_IR0_MODE_msk;
        result = xensiv_bgt60cutrxx_set_reg(&sensor.dev, BGT60CUTR13B_REG_IR_CTRL, ir_ctrl);
    }
    if (XENSIV_BGT60CUTRXX_STATUS_OK != result)
    {
        protocol_print_error("Could not switch the sensor interrupt to pulse mode");
        return 1;
    }

    result = xensiv_bgt60cutrxx_platform_irq_init(&sensor, &mcu_iface_config, &xensiv_bgt60cutrxx_irq_handler, NULL);
    if (XENSIV_BGT60CUTRXX_STATUS_OK != result)
    {
        protocol_print_error("Could not initialize the radar IRQ handler");
        return 1;
    }

    result = xensiv_bgt60cutrxx_set_irq_clear(&sensor.dev, BGT60CUTR13B_IR_CLEAR_FOR_STARTUP_MSK);
    if (XENSIV_BGT60CUTRXX_STATUS_OK != result)
    {
        protocol_print_error("Could not clear the sensor IRQ flags");
        return 1;
    }

    protocol_set_app_info(USE_CASE_NAME, VERSION, VERSION_BUILD);
    protocol_set_sensor_info("BGT60CUTR13B", VERSION, false, 80.0f);

    if (rtt_stream_init(rtt_stream_buffer, sizeof(rtt_stream_buffer)) != 0)
    {
        protocol_print_error("Could not initialize the RTT stream");
        return 1;
    }

    rtt_info.control_block = rtt_stream_get_control_block();

    // the register configuration decides the alignment, so report what the sensor actually does
    uint32_t spi_ctrl = 0;
    if (XENSIV_BGT60CUTRXX_STATUS_OK !=
        xensiv_bgt60cutrxx_get_reg(&sensor.dev, BGT60CUTR13B_REG_SPI_CTRL, &spi_ctrl))
    {
        protocol_print_error("Could not read the sample alignment from the sensor");
        return 1;
    }
    rtt_info.msb_aligned = (spi_ctrl & BGT60CUTR13B_SPI_CTRL_MSB_ALIGN_EN_msk) != 0U;

    rtt_info.adc_length_bytes = dsp_chain.adc_length_bytes;
    rtt_info.rfft_length_bytes = dsp_chain.rfft_length_bytes;
    rtt_info.dfft_length_bytes = dsp_chain.dfft_length_bytes;

    protocol_set_rtt_info(&rtt_info);

    protocol_print_info("Radar frame size: %" PRIu32 " FIFO words", (uint32_t)NUM_FRAME_WORDS);
    protocol_print_info("FIFO IRQ threshold: %" PRIu32 " FIFO words", (uint32_t)NUM_THRESHOLD_WORDS);

    if ((NUM_FRAME_WORDS % NUM_THRESHOLD_WORDS) != 0U)
    {
        // ADR 0003: a radar frame must be a whole number of interrupts, or the stream is
        // silently misaligned and the host cannot tell
        protocol_print_error("FIFO threshold %" PRIu32 " does not divide radar frame size %" PRIu32,
                             (uint32_t)NUM_THRESHOLD_WORDS, (uint32_t)NUM_FRAME_WORDS);
        return 1;
    }
    protocol_print_info("Radar frame slices: %" PRIu32, (uint32_t)NUM_SLICES_PER_FRAME);

    frame_assembler_init(&frame_assembler, NUM_SLICES_PER_FRAME);

    #if PRINT_HEART_BEAT_DEBUG_INFO
    if (cyhal_lptimer_init(&heartbeat_timer) != CY_RSLT_SUCCESS)
    {
        protocol_print_error("Could not initialize the heartbeat timer");
        return 1;
    }

    cyhal_lptimer_info_t timer_info;
    cyhal_lptimer_get_info(&heartbeat_timer, &timer_info);
    #endif

    // whatever the sensor buffered before the first slice is read would offset every later slice
    // by the same amount, so the stream would stay misaligned with the acquisition boundaries
    clear_fifo(dsp_chain.forwarding_mask);

    result = xensiv_bgt60cutrxx_set_motion_detection(&sensor.dev, 0, true);
    if (XENSIV_BGT60CUTRXX_STATUS_OK != result)
    {
        protocol_print_error("Could not start the radar data acquisition sequence");
        return 1;
    }
    protocol_print_info("Radar data acquisition started");

    uint32_t counter = 0;
    uint32_t frames_dropped = 0;

    #if PRINT_HEART_BEAT_DEBUG_INFO
    const uint32_t heartbeat_ticks = timer_info.frequency_hz * HEARTBEAT_INTERVAL_S;
    uint32_t heartbeat_last = cyhal_lptimer_read(&heartbeat_timer);
    uint32_t irqs = 0;
    #endif

    while (true)
    {
        protocol_handler(counter);

        #if PRINT_HEART_BEAT_DEBUG_INFO
        const uint32_t now = cyhal_lptimer_read(&heartbeat_timer);
        if ((now - heartbeat_last) >= heartbeat_ticks)
        {
            heartbeat_last = now;
            protocol_print_info("Heartbeat: %" PRIu32 " IRQs, %" PRIu32 " frames, %" PRIu32 " dropped",
                                irqs, counter, frames_dropped);
            report_sensor_status();
        }
        #endif

        if (!radar_event_irq)
        {
            continue;
        }
        radar_event_irq = false;
        #if PRINT_HEART_BEAT_DEBUG_INFO
        irqs++;
        #endif

        cyhal_gpio_write(SHIELD_XENSIV_RADAR_LED_GREEN, SHIELD_XENSIV_RADAR_LED_GREEN_ON);

        xensiv_bgt60cutrxx_fifo_events_t event;
        bool frame_valid = false;

        if (XENSIV_BGT60CUTRXX_STATUS_OK != radar_fifo_check_integrity(&sensor.dev, &event, &frame_valid))
        {
            protocol_print_error("Could not read the FIFO event");
            frame_assembler_reset(&frame_assembler);
            continue;
        }

        if (!frame_valid)
        {
            frames_dropped++;
            clear_fifo(dsp_chain.forwarding_mask);
            frame_assembler_reset(&frame_assembler);
            report_frames_dropped(frames_dropped);
            continue;
        }

        uint16_t* slice = &frame_data_buffer[FIFO_WORDS_TO_ADC_SAMPLES(frame_assembler_slice_index(&frame_assembler) *
                                                                       NUM_THRESHOLD_WORDS)];

        if (XENSIV_BGT60CUTRXX_STATUS_OK !=
            radar_fifo_read(&sensor.dev, slice, FIFO_WORDS_TO_ADC_SAMPLES(NUM_THRESHOLD_WORDS)))
        {
            protocol_print_error("Could not read the radar frame from FIFO");
            frames_dropped++;
            frame_assembler_reset(&frame_assembler);
            report_frames_dropped(frames_dropped);
            continue;
        }

        if (frame_assembler_accept_slice(&frame_assembler))
        {
            if (!rtt_stream_write_frame(frame_data_buffer, FIFO_WORDS_TO_ADC_SAMPLES(NUM_FRAME_WORDS), counter))
            {
                frames_dropped++;
                report_frames_dropped(frames_dropped);
            }
            counter++;
        }

        cyhal_gpio_write(SHIELD_XENSIV_RADAR_LED_GREEN, SHIELD_XENSIV_RADAR_LED_GREEN_OFF);
    }
}


/* [] END OF FILE */
