/******************************************************************************
 * File Name: rtt_stream_header.h
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

#ifndef RTT_STREAM_HEADER_H_
#define RTT_STREAM_HEADER_H_

#include <stdint.h>

/** Magic bytes the host parser synchronises on, "[START]"; bytes rather than a string literal
 * so the header is copied between objects of the same type */
#define RTT_STREAM_MAGIC        { '[', 'S', 'T', 'A', 'R', 'T', ']' }

/** Length of RTT_STREAM_MAGIC, without its terminating NUL */
#define RTT_STREAM_MAGIC_LEN    7U

/** Magic, frame number and payload length */
#define RTT_STREAM_HEADER_LEN   (RTT_STREAM_MAGIC_LEN + 4U + 4U)

/**
 * @brief Serialises an RTT Stream Frame header into a caller-provided buffer.
 *
 * The header is exactly RTT_STREAM_HEADER_LEN bytes: the magic at offset 0, the frame number
 * little-endian at offset 7 and the payload length little-endian at offset 11. The frame
 * number wraps at UINT32_MAX without a special case, so a gap in the sequence observed by the
 * host is evidence of a dropped frame.
 *
 * @param header Buffer of at least RTT_STREAM_HEADER_LEN bytes.
 * @param frame_number Monotonic radar frame counter.
 * @param payload_length_bytes Size of the raw ADC payload that follows, in bytes.
 */
void rtt_stream_write_header(uint8_t* header, uint32_t frame_number, uint32_t payload_length_bytes);

#endif /* RTT_STREAM_HEADER_H_ */
