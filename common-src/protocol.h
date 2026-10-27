#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
* Macros
*******************************************************************************/

/** Sync byte for frame start */
#define PROTOCOL_SYNC_BYTE          0xD9

/** Minimum frame size: sync + cmd + len(2) + crc(2) */
#define PROTOCOL_MIN_FRAME_SIZE     6

/** Maximum payload size (adjust as needed) */
#define PROTOCOL_MAX_PAYLOAD_SIZE   256

/** Circular buffer size (must be power of 2 for efficiency) */
#define PROTOCOL_BUFFER_SIZE        512

/*******************************************************************************
* Types
*******************************************************************************/
/* Terminal command types */
typedef enum
{
    CMD_EMPTY = 0,

    /**
     * @brief Get information about the system. Firmware version, sensor information, general system information.
     */
    CMD_GET_INFO = 1
} protocol_cmd_t;

/** Protocol parser states */
typedef enum
{
    PROTOCOL_STATE_WAIT_SYNC,       /**< Waiting for sync byte */
    PROTOCOL_STATE_WAIT_CMD,        /**< Waiting for command ID */
    PROTOCOL_STATE_WAIT_LEN_LOW,    /**< Waiting for length low byte */
    PROTOCOL_STATE_WAIT_LEN_HIGH,   /**< Waiting for length high byte */
    PROTOCOL_STATE_WAIT_PAYLOAD,    /**< Waiting for payload bytes */
    PROTOCOL_STATE_WAIT_CRC_LOW,    /**< Waiting for CRC low byte */
    PROTOCOL_STATE_WAIT_CRC_HIGH    /**< Waiting for CRC high byte */
} protocol_state_t;

/** Command frame structure */
typedef struct
{
    uint8_t  sync;                              /**< Sync byte (0xD9) */
    uint8_t  cmd_id;                            /**< Command ID */
    uint16_t payload_length;                    /**< Payload length */
    uint8_t  payload[PROTOCOL_MAX_PAYLOAD_SIZE]; /**< Payload data */
    uint16_t crc;                               /**< CRC checksum */
} protocol_message_t;

/** Radar acquisition parameters of a streamed radar frame */
typedef struct
{
    uint32_t num_tx;                    /**< Number of transmit antennas */
    uint32_t num_rx;                    /**< Number of receive antennas */
    uint32_t num_chirps;                /**< Number of chirps per radar frame */
    uint32_t num_samples_per_chirp;     /**< Number of samples per chirp */
    /** Distance between two range bins in meters, which the host needs to label a range axis.
     * 0 when the application does not know it. */
    float range_resolution_m;
} protocol_radar_config_t;

/** Description of the RTT stream, as the host needs it to attach and decode */
typedef struct
{
    /** Address of the SEGGER RTT control block, as an integer because it is only ever reported
     * to the host as a number and never dereferenced here */
    uintptr_t control_block;
    uint32_t adc_length_bytes;          /**< Size of the raw ADC section, 0 if disabled */
    uint32_t rfft_length_bytes;         /**< Size of the range FFT section, 0 if disabled */
    uint32_t dfft_length_bytes;         /**< Size of the Doppler FFT section, 0 if disabled */
    /** True if the sensor left-aligns a sample in its transfer word (SPI_CTRL.MSB_ALIGN_EN).
     * The host needs it to scale a sample and it differs between configurations, so read it
     * back from the sensor rather than hard-coding it. */
    bool msb_aligned;
    protocol_radar_config_t radar_config; /**< Acquisition parameters, authoritative for the host */
} protocol_rtt_info_t;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/

/**
 * @brief Sets the application information.
 *
 * This function sets the application information including the application name,
 * version, and build.
 *
 * @param app_name The name of the application.
 * @param version The version of the application.
 * @param build The build identifier of the application.
 */
void protocol_set_app_info(const char* app_name, const char* version, const char* build);

/**
 * @brief Sets the sensor information.
 *
 * This function sets the sensor information including the sensor name,
 * driver version, configurability, and crystal frequency.
 *
 * @param sensor_name The name of the sensor.
 * @param driver_version The version of the sensor driver.
 * @param is_configurable Indicates if the sensor is configurable.
 * @param xtal_mhz The crystal frequency in MHz.
 */
void protocol_set_sensor_info(const char* sensor_name, const char* driver_version, bool is_configurable,
                              float xtal_mhz);

/**
 * @brief Registers the description of the RTT stream.
 *
 * Only an application that streams radar frames over RTT calls this. The get_info response
 * carries an `rtt` block only once it has been called, so the response of every other
 * application is unchanged.
 *
 * The structure is stored by reference and must outlive every get_info response, so it is
 * expected to have static storage duration.
 *
 * @param info Description of the stream, or NULL to stop reporting one.
 */
void protocol_set_rtt_info(const protocol_rtt_info_t* info);

/**
 * @brief Sets the zoning configuration.
 *
 * This function sets the zoning configuration for the algorithm.
 *
 * @param config Pointer to the zoning configuration structure.
 */
void protocol_set_zone_config(const algo_zoning_event_t* config);

/**
 * @brief Sets the clutter localization reference for on-demand reporting.
 *
 * When set, the protocol handler will print the current clutter zone state
 * as part of the get_info response.
 *
 * @param clutter Pointer to the clutter localization instance (or NULL to disable).
 */
void protocol_set_clutter_info(clutter_localization_t* clutter);

/**
 * @brief Prints informational messages in a structured format.
 *
 * This function takes a formatted string and its arguments, formats the message,
 * and prints it in a JSON-like structure for easy parsing on the receiving end.
 *
 * @param format The format string (printf-style).
 * @param ... Additional arguments for the format string.
 */
void protocol_print_info(const char* format, ...);

/**
 * @brief Prints error messages in a structured format.
 *
 * This function takes a formatted string and its arguments, formats the message,
 * and prints it in a JSON-like structure for easy parsing on the receiving end.
 *
 * @param format The format string (printf-style).
 * @param ... Additional arguments for the format string.
 */
void protocol_print_error(const char* format, ...);

/**
 * @brief Reports the cumulative number of radar frames lost, in the form the host parses.
 *
 * Emits a single `{"frames_dropped": N}` line. The host classifies this message by its
 * field name, so a generic info message is not an equivalent substitute.
 *
 * @param frames_dropped Cumulative number of radar frames that could not be delivered.
 */
void protocol_print_frames_dropped(uint32_t frames_dropped);

/**
 * @brief Handles the protocol operations.
 *
 * This function processes the protocol tasks and should be called
 * periodically (e.g., in the main loop) to ensure proper protocol operation.
 * It reads data from the serial port, parses incoming frames, and dispatches
 * complete frames to the command handler.
 *
 * This function is non-blocking and maintains internal state between calls.
 * @param counter The current timestamp or counter value.
 */
void protocol_handler(uint32_t counter);
#endif // PROTOCOL_H
