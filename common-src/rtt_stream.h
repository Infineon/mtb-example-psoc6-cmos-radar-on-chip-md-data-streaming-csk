/******************************************************************************
 * File Name: rtt_stream.h
 *
 * Description: Transport of RTT Stream Frames to the host over SEGGER RTT.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2026-YEAR Cypress Semiconductor $
 *******************************************************************************/

#ifndef RTT_STREAM_H_
#define RTT_STREAM_H_

#include <stdbool.h>
#include <stdint.h>

/** Default size of the up-buffer the caller has to provide; holds ten radar frames of the
 * fifo-read example. An example with larger frames sizes its own buffer instead, since
 * rtt_stream_init() takes the size. */
#define RTT_STREAM_BUFFER_SIZE (65536U)

/**
 * @brief Configures the RTT up-buffer used for radar frames.
 *
 * Must be called once before rtt_stream_write_frame().
 *
 * @param buffer Up-buffer memory, owned by SEGGER RTT from this call onwards, so it has to
 *               remain valid for the lifetime of the stream.
 * @param buffer_size Size of @p buffer in bytes; use RTT_STREAM_BUFFER_SIZE.
 * @return 0 on success, -1 if the channel could not be configured.
 */
int32_t rtt_stream_init(uint8_t* buffer, uint32_t buffer_size);

/**
 * @brief Returns the address of the SEGGER RTT control block.
 *
 * The host needs this address to attach its debug probe without searching target memory.
 *
 * @return Address of the control block, as an integer for reporting to the host.
 */
uintptr_t rtt_stream_get_control_block(void);

/**
 * @brief Writes one RTT Stream Frame, header and payload, to the host.
 *
 * The frame is written whole or not at all: if the up-buffer cannot take the complete frame,
 * nothing is written and the host sees a gap in the frame numbers rather than a truncated
 * payload.
 *
 * @param frame radar frame samples, sent verbatim in the FIFO's native interleaving.
 * @param num_samples Number of samples in @p frame.
 * @param frame_number Monotonic radar frame counter.
 * @return true if the frame was written, false if it was dropped for want of buffer space.
 */
bool rtt_stream_write_frame(const uint16_t* frame, uint32_t num_samples, uint32_t frame_number);

#endif /* RTT_STREAM_H_ */
