/*
 * protocol.c
 *
 * Description: This source file implements a non-blocking serial protocol handler
 *              for receiving and parsing command messages from a host.
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
#include <inttypes.h>
 #include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "common.h"
#include "cyhal.h"
#include "cyhal_system.h"
#include "cy_retarget_io.h"
#include "protocol.h"

/*******************************************************************************
* Macros
*******************************************************************************/

/** Mask for circular buffer index wrapping (size must be power of 2) */
#define BUFFER_INDEX_MASK   (PROTOCOL_BUFFER_SIZE - 1)

/*******************************************************************************
* Static Variables
*******************************************************************************/
static const algo_zoning_event_t* zoning_config = NULL;
static clutter_localization_t* clutter_ref = NULL;

/** Circular buffer for received data */
static uint8_t rx_buffer[PROTOCOL_BUFFER_SIZE];

/** Write index for circular buffer */
static volatile uint16_t write_idx = 0;

/** Read index for circular buffer */
static volatile uint16_t read_idx = 0;

/** Current parser state */
static protocol_state_t parser_state = PROTOCOL_STATE_WAIT_SYNC;

/** Current message being parsed */
static protocol_message_t current_message;

/** Payload bytes received counter */
static uint16_t payload_bytes_received = 0;

static const char* protocol_app_name = NULL;
static const char* protocol_app_version = NULL;
static const char* protocol_app_build = NULL;
static const char* protocol_sensor_name = NULL;
static const char* protocol_sensor_driver_version = NULL;
static bool protocol_sensor_is_configurable = false;
static float protocol_sensor_crystal_freq_mhz = 0.0f;
static const protocol_rtt_info_t* protocol_rtt_info = NULL;

/*******************************************************************************
* Static Function Prototypes
*******************************************************************************/

static uint16_t buffer_get_available_bytes(void);
static uint8_t buffer_read_byte(void);
static void read_serial_data(void);
static uint32_t process_message(protocol_message_t* message, uint32_t counter);
static uint32_t command_handler(protocol_message_t* message, uint32_t counter);
static uint16_t calculate_crc(const uint8_t* data, uint16_t length);
static uint32_t get_system_clock_frequency(void);
static const char* get_system_cpu_type(void);

/*******************************************************************************
* Function Implementations
*******************************************************************************/

/**
 * @brief Sets the application information.
 */
void protocol_set_app_info(const char* app_name, const char* version, const char* build)
{
    protocol_app_name = app_name;
    protocol_app_version = version;
    protocol_app_build = build;
    // Reset circular buffer indices
    write_idx = 0;
    read_idx = 0;

    // Initialize parser state
    parser_state = PROTOCOL_STATE_WAIT_SYNC;
    payload_bytes_received = 0;

    // Clear the message structure
    memset(&current_message, 0, sizeof(protocol_message_t));
}


void protocol_set_sensor_info(const char* sensor_name, const char* driver_version, bool is_configurable, float xtal_mhz)
{
    protocol_sensor_name = sensor_name;
    protocol_sensor_driver_version = driver_version;
    protocol_sensor_is_configurable = is_configurable;
    protocol_sensor_crystal_freq_mhz = xtal_mhz;
}


void protocol_set_rtt_info(const protocol_rtt_info_t* info)
{
    protocol_rtt_info = info;
}


void protocol_set_zone_config(const algo_zoning_event_t* config)
{
    zoning_config = config;
}


void protocol_set_clutter_info(clutter_localization_t* clutter)
{
    clutter_ref = clutter;
}


void protocol_print_info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    printf("{\"info\":\"");
    vprintf(format, args);
    printf("\"}\n");
    va_end(args);
}


void protocol_print_error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    printf("{\"error\":\"");
    vprintf(format, args);
    printf("\"}\n");
    va_end(args);
}


void protocol_print_frames_dropped(uint32_t frames_dropped)
{
    printf("{\"frames_dropped\": %" PRIu32 "}\n", frames_dropped);
}


/**
 * @brief Main protocol handler - non-blocking state machine.
 */
void protocol_handler(uint32_t counter)
{
    // Read available data from serial port into circular buffer
    read_serial_data();

    // Process the buffer based on current state
    while (buffer_get_available_bytes() > 0)
    {
        uint8_t byte;

        switch (parser_state)
        {
            case PROTOCOL_STATE_WAIT_SYNC:
                byte = buffer_read_byte();
                if (byte == PROTOCOL_SYNC_BYTE)
                {
                    current_message.sync = byte;
                    parser_state = PROTOCOL_STATE_WAIT_CMD;
                }
                // If not sync byte, just discard it (already read)
                break;

            case PROTOCOL_STATE_WAIT_CMD:
                current_message.cmd_id = buffer_read_byte();
                parser_state = PROTOCOL_STATE_WAIT_LEN_LOW;
                break;

            case PROTOCOL_STATE_WAIT_LEN_LOW:
                byte = buffer_read_byte();
                current_message.payload_length = byte; // Low byte
                parser_state = PROTOCOL_STATE_WAIT_LEN_HIGH;
                break;

            case PROTOCOL_STATE_WAIT_LEN_HIGH:
                byte = buffer_read_byte();
                current_message.payload_length |= ((uint16_t)byte << 8U); // High byte

                // Validate payload length
                if (current_message.payload_length > PROTOCOL_MAX_PAYLOAD_SIZE)
                {
                    // Invalid length - reset to sync search
                    parser_state = PROTOCOL_STATE_WAIT_SYNC;
                }
                else if (current_message.payload_length == 0)
                {
                    // No payload, go straight to CRC
                    payload_bytes_received = 0;
                    parser_state = PROTOCOL_STATE_WAIT_CRC_LOW;
                }
                else
                {
                    // Valid length, start receiving payload
                    payload_bytes_received = 0;
                    parser_state = PROTOCOL_STATE_WAIT_PAYLOAD;
                }
                break;

            case PROTOCOL_STATE_WAIT_PAYLOAD:
                current_message.payload[payload_bytes_received++] = buffer_read_byte();

                if (payload_bytes_received >= current_message.payload_length)
                {
                    // All payload bytes received, move to CRC
                    parser_state = PROTOCOL_STATE_WAIT_CRC_LOW;
                }
                break;

            case PROTOCOL_STATE_WAIT_CRC_LOW:
                byte = buffer_read_byte();
                current_message.crc = byte; // Low byte
                parser_state = PROTOCOL_STATE_WAIT_CRC_HIGH;
                break;

            case PROTOCOL_STATE_WAIT_CRC_HIGH:
                byte = buffer_read_byte();
                current_message.crc |= ((uint16_t)byte << 8U); // High byte

                // Message complete - process it
                process_message(&current_message, counter);

                // Reset to wait for next message
                parser_state = PROTOCOL_STATE_WAIT_SYNC;
                payload_bytes_received = 0;
                break;

            default:
                // Invalid state - reset
                parser_state = PROTOCOL_STATE_WAIT_SYNC;
                break;
        }
    }
}


/**
 * @brief Read available data from serial port into circular buffer.
 *
 * This function uses non-blocking UART reads by accessing the cy_retarget_io
 * UART object directly. It checks for available data before attempting to read.
 */
static void read_serial_data(void)
{
    uint8_t byte;

    // Read all available bytes from UART (non-blocking)
    while (cyhal_uart_readable(&cy_retarget_io_uart_obj) > 0)
    {
        // Attempt non-blocking read (timeout = 0)
        cy_rslt_t result = cyhal_uart_getc(&cy_retarget_io_uart_obj, &byte, 0);

        if (result != CY_RSLT_SUCCESS)
        {
            // No data available or error
            break;
        }

        // Check if buffer is full
        uint16_t current_write_idx = write_idx;
        uint16_t current_read_idx = read_idx;
        if (((current_write_idx + 1) & BUFFER_INDEX_MASK) == current_read_idx)
        {
            // Buffer full - discard oldest byte to make room
            current_read_idx = (current_read_idx + 1) & BUFFER_INDEX_MASK;
            read_idx = current_read_idx;
        }

        // Write byte to circular buffer
        rx_buffer[current_write_idx] = byte;
        current_write_idx = (current_write_idx + 1) & BUFFER_INDEX_MASK;
        write_idx = current_write_idx;
    }
}


/**
 * @brief Get number of available bytes in circular buffer.
 */
static uint16_t buffer_get_available_bytes(void)
{
    uint16_t current_write_idx = write_idx;
    uint16_t current_read_idx = read_idx;
    return (current_write_idx - current_read_idx) & BUFFER_INDEX_MASK;
}


/**
 * @brief Read one byte from circular buffer.
 */
static uint8_t buffer_read_byte(void)
{
    uint16_t current_read_idx = read_idx;
    uint8_t byte = rx_buffer[current_read_idx];
    current_read_idx = (current_read_idx + 1) & BUFFER_INDEX_MASK;
    read_idx = current_read_idx;
    return byte;
}


/**
 * @brief Process a complete message.
 *
 * @param message Pointer to the complete message structure.
 * @param counter The current timestamp or counter value.
 */
static uint32_t process_message(protocol_message_t* message, uint32_t counter)
{
    // Calculate CRC over: sync + cmd_id + length + payload
    uint8_t crc_buffer[4 + PROTOCOL_MAX_PAYLOAD_SIZE];
    uint16_t crc_length = 0;

    // Build CRC calculation buffer
    crc_buffer[crc_length++] = message->sync;
    crc_buffer[crc_length++] = message->cmd_id;
    crc_buffer[crc_length++] = (uint8_t)(message->payload_length & 0xFF);
    crc_buffer[crc_length++] = (uint8_t)(message->payload_length >> 8U);

    // Copy payload
    memcpy(&crc_buffer[crc_length], message->payload, message->payload_length);
    crc_length += message->payload_length;

    // Calculate and verify CRC
    uint16_t calculated_crc = calculate_crc(crc_buffer, crc_length);

    if (calculated_crc == message->crc)
    {
        // CRC valid - call command handler
        return command_handler(message, counter);
    }

    return 0;
}


static void handle_get_info(void)
{
    const char* name = protocol_app_name != NULL ? protocol_app_name : "Radar Application";
    const char* version = protocol_app_version != NULL ? protocol_app_version : "0.0.0";
    const char* build = protocol_app_build != NULL ? protocol_app_build : "000000";
    const char* cpu = get_system_cpu_type();
    uint32_t cpu_freq = get_system_clock_frequency();

    printf("{\"version\": \"%s\", \"name\": \"%s\", \"build\": \"%s\", \"cpu_freq\": %" PRIu32 ", \"cpu\": \"%s\"",
           version,
           name,
           build,
           cpu_freq,
           cpu);

    printf(
        ",\"event_info\":{\"types\":{\"azimuth_clutter\":\"zones\",\"elevation_clutter\":\"zones\"}, \"detection_type_mapping\":{\"1\":\"ULP\", \"2\":\"RD\", \"3\":\"ULP Clustered\"}}");

    printf(
        ",\"sensor_info\":{\"driver_version\":\"%s\",\"name\": \"%s\", \"is_configurable\": %s, \"xtal_mhz\": %.2f}",
        protocol_sensor_driver_version != NULL ? protocol_sensor_driver_version : "Unknown",
        protocol_sensor_name != NULL ? protocol_sensor_name : "BGT60CUTRXX",
        protocol_sensor_is_configurable ? "true" : "false",
        protocol_sensor_crystal_freq_mhz);

    if (protocol_rtt_info != NULL)
    {
        // the key order of data_and_payload_length_bytes defines the on-wire section order
        printf(
            ",\"rtt\":{\"addr\": \"0x%08" PRIXPTR "\", \"data_and_payload_length_bytes\":{\"adc\": %" PRIu32 ", \"rfft\": %" PRIu32 ", \"dfft\": %" PRIu32 "}, \"msb_aligned\": %s, \"radar_config\":{\"num_tx\": %" PRIu32 ", \"num_rx\": %" PRIu32 ", \"num_chirps\": %" PRIu32 ", \"num_samples_per_chirp\": %" PRIu32 ", \"range_resolution_m\": %.6f}}",
            protocol_rtt_info->control_block,
            protocol_rtt_info->adc_length_bytes,
            protocol_rtt_info->rfft_length_bytes,
            protocol_rtt_info->dfft_length_bytes,
            protocol_rtt_info->msb_aligned ? "true" : "false",
            protocol_rtt_info->radar_config.num_tx,
            protocol_rtt_info->radar_config.num_rx,
            protocol_rtt_info->radar_config.num_chirps,
            protocol_rtt_info->radar_config.num_samples_per_chirp,
            (double)protocol_rtt_info->radar_config.range_resolution_m);
    }

    printf("}\n");
}


/**
 * @brief Command handler - processes valid messages.
 *
 * This function should be implemented to handle different command IDs
 * and execute the appropriate actions based on the received command.
 *
 * @param message Pointer to the validated message structure.
 * @param counter The current timestamp or counter value.
 */
static uint32_t command_handler(protocol_message_t* message, uint32_t counter)
{
    uint32_t result = 0;
    switch (message->cmd_id)
    {
        case CMD_GET_INFO:
            handle_get_info();
            result = 1;
            break;

        default:
            /* Unknown command - result remains 0 */
            result = 0;
            break;
    }

    printf("{\"cmd\": { \"id\": %d, \"state\": %" PRIu32 " }}\n", message->cmd_id, result);

    if (result && (zoning_config != NULL))
    {
        print_algo_zoning_event(zoning_config, counter);
    }
    if (result && (clutter_ref != NULL))
    {
        print_clutter_zones_current(clutter_ref, counter);
    }
    return result;
}


/**
 * @brief Calculate CRC-16 checksum (CCITT variant).
 *
 * @param data Pointer to data buffer.
 * @param length Length of data in bytes.
 * @return Calculated CRC-16 value.
 */
static uint16_t calculate_crc(const uint8_t* data, uint16_t length)
{
    uint16_t crc = 0xFFFF; // Initial value

    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= ((uint16_t)data[i] << 8U);

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1U) ^ 0x1021; // CRC-16-CCITT polynomial
            }
            else
            {
                crc = crc << 1U;
            }
        }
    }

    return crc;
}


static uint32_t get_system_clock_frequency(void)
{
    #if defined(COMPONENT_CM4)
    return SystemCoreClock;
    #else
    //#error "SystemCoreClock not defined"
    return 0;
    #endif
}


static const char* get_system_cpu_type(void)
{
    #if defined(COMPONENT_CM4)
    return "Cortex-M4";
    #else
    //TODO: error case appears on qemu
    //#error "CPU type not defined."
    return "Undefined";
    #endif
}
