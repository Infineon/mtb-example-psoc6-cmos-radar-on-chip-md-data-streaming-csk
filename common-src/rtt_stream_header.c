/******************************************************************************
 * File Name: rtt_stream_header.c
 *
 * Description: Serialisation of the RTT Stream Frame header that precedes every radar frame
 *              sent to the host.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2026-YEAR Cypress Semiconductor $
 *******************************************************************************/

#include "rtt_stream_header.h"
#include <string.h>

/*******************************************************************************
* Macros
*******************************************************************************/

/** Offset of the frame number field within the header */
#define FRAME_NUMBER_OFFSET     RTT_STREAM_MAGIC_LEN

/** Offset of the payload length field within the header */
#define PAYLOAD_LENGTH_OFFSET   (RTT_STREAM_MAGIC_LEN + 4U)

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static void write_u32_le(uint8_t* destination, uint32_t value);

/*******************************************************************************
* Static Variables
*******************************************************************************/

/** Magic the host parser synchronises on */
static const uint8_t rtt_stream_magic[RTT_STREAM_MAGIC_LEN] = RTT_STREAM_MAGIC;

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/**
 * @brief Writes a 32-bit value in little-endian byte order.
 *
 * Done byte by byte so the header is identical on any host, independent of the endianness and
 * alignment rules of the compiling machine.
 *
 * @param[out] destination Buffer of at least four bytes.
 * @param[in] value Value to serialise.
 */
static void write_u32_le(uint8_t* destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)((value >> 8) & 0xFFU);
    destination[2] = (uint8_t)((value >> 16) & 0xFFU);
    destination[3] = (uint8_t)((value >> 24) & 0xFFU);
}


void rtt_stream_write_header(uint8_t* header, uint32_t frame_number, uint32_t payload_length_bytes)
{
    memcpy(header, rtt_stream_magic, RTT_STREAM_MAGIC_LEN);
    write_u32_le(&header[FRAME_NUMBER_OFFSET], frame_number);
    write_u32_le(&header[PAYLOAD_LENGTH_OFFSET], payload_length_bytes);
}
