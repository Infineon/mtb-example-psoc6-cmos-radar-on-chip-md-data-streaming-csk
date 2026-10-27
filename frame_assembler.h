/******************************************************************************
 * File Name: frame_assembler.h
 *
 * Description: Assembly of one radar frame from several FIFO threshold interrupts.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2026-YEAR Cypress Semiconductor $
 *******************************************************************************/

#ifndef FRAME_ASSEMBLER_H_
#define FRAME_ASSEMBLER_H_

#include <stdbool.h>
#include <stdint.h>

/** Tracks which slice of the current radar frame is read next.
 *
 * A range FFT frame is larger than the FIFO, so it arrives as several equally sized slices,
 * one per threshold interrupt. The host must never see a frame stitched together from two
 * different acquisitions, which is what makes the reset on error part of the contract rather
 * than a convenience.
 */
typedef struct
{
    uint32_t slices_per_frame;  /**< Threshold interrupts that make up one radar frame */
    uint32_t slice_index;       /**< Slice the next read fills, 0 while no frame is in progress */
} frame_assembler_t;

/**
 * @brief Prepares the assembler for frames of @p slices_per_frame slices.
 *
 * @param assembler Assembler to initialize.
 * @param slices_per_frame Number of threshold interrupts that deliver one whole radar frame.
 */
void frame_assembler_init(frame_assembler_t* assembler, uint32_t slices_per_frame);

/**
 * @brief Returns the slice the next read has to fill.
 *
 * Multiply it by the slice length to get the offset into the frame buffer.
 *
 * @param assembler Assembler to query.
 * @return Index of the next slice, counted from 0.
 */
uint32_t frame_assembler_slice_index(const frame_assembler_t* assembler);

/**
 * @brief Records that the slice at the current index has been read.
 *
 * @param assembler Assembler to advance.
 * @return true if the radar frame is now complete and the buffer can be sent.
 */
bool frame_assembler_accept_slice(frame_assembler_t* assembler);

/**
 * @brief Discards a partially read radar frame.
 *
 * Call this whenever a slice could not be read or trusted. Without it the orphaned slice
 * would be paired with the first slice of the next acquisition and every later frame would
 * stay misaligned.
 *
 * @param assembler Assembler to reset.
 */
void frame_assembler_reset(frame_assembler_t* assembler);

#endif /* FRAME_ASSEMBLER_H_ */
