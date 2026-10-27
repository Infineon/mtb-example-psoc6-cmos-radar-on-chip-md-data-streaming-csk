/******************************************************************************
 * File Name: frame_assembler.c
 *
 * Description: Assembly of one radar frame from several FIFO threshold interrupts.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2026-YEAR Cypress Semiconductor $
 *******************************************************************************/

#include "frame_assembler.h"

void frame_assembler_init(frame_assembler_t* assembler, uint32_t slices_per_frame)
{
    assembler->slices_per_frame = slices_per_frame;
    assembler->slice_index = 0U;
}


uint32_t frame_assembler_slice_index(const frame_assembler_t* assembler)
{
    return assembler->slice_index;
}


bool frame_assembler_accept_slice(frame_assembler_t* assembler)
{
    assembler->slice_index++;

    if (assembler->slice_index < assembler->slices_per_frame)
    {
        return false;
    }

    assembler->slice_index = 0U;
    return true;
}


void frame_assembler_reset(frame_assembler_t* assembler)
{
    assembler->slice_index = 0U;
}


/* [] END OF FILE */
