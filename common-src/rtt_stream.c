/******************************************************************************
 * File Name: rtt_stream.c
 *
 * Description: Transport of RTT Stream Frames to the host over SEGGER RTT.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2026-YEAR Cypress Semiconductor $
 *******************************************************************************/

#include "rtt_stream.h"
#include "rtt_stream_header.h"
#include "SEGGER_RTT.h"
#include <stddef.h>

/*******************************************************************************
* Macros
*******************************************************************************/

/** Up-buffer carrying radar frames; channel 0 stays SEGGER's terminal */
#define RTT_STREAM_CHANNEL      (1U)

/** Name the host sees for the stream channel */
#define RTT_STREAM_CHANNEL_NAME "RadarData"

/*******************************************************************************
* Function Definitions
*******************************************************************************/

int32_t rtt_stream_init(uint8_t* buffer, uint32_t buffer_size)
{
    if (buffer == NULL)
    {
        return -1;
    }

    if (SEGGER_RTT_ConfigUpBuffer(RTT_STREAM_CHANNEL, RTT_STREAM_CHANNEL_NAME, buffer,
                                  (unsigned)buffer_size, SEGGER_RTT_MODE_NO_BLOCK_SKIP) != 0)
    {
        return -1;
    }

    return 0;
}


uintptr_t rtt_stream_get_control_block(void)
{
    return (uintptr_t)&_SEGGER_RTT;
}


bool rtt_stream_write_frame(const uint16_t* frame, uint32_t num_samples, uint32_t frame_number)
{
    if (frame == NULL)
    {
        return false;
    }

    const uint32_t payload_length_bytes = num_samples * (uint32_t)sizeof(uint16_t);
    uint8_t header[RTT_STREAM_HEADER_LEN];

    // a frame is written whole or not at all, so the host never sees a truncated payload
    if (SEGGER_RTT_GetAvailWriteSpace(RTT_STREAM_CHANNEL) < (RTT_STREAM_HEADER_LEN + payload_length_bytes))
    {
        return false;
    }

    rtt_stream_write_header(header, frame_number, payload_length_bytes);

    (void)SEGGER_RTT_Write(RTT_STREAM_CHANNEL, header, RTT_STREAM_HEADER_LEN);
    (void)SEGGER_RTT_Write(RTT_STREAM_CHANNEL, frame, payload_length_bytes);

    return true;
}
