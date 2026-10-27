/******************************************************************************
 * File Name: dsp_chain.c
 *
 * Description: Selection of the sensor processing chain stages that are streamed to the host.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2026-YEAR Cypress Semiconductor $
 *******************************************************************************/

#include <stddef.h>
#include "dsp_chain.h"
#include "xensiv_bgt60cutrxxb_register_hal.h"

/** Every FIFO word reaches the host as four bytes. */
#define WORDS_TO_BYTES(words) ((words) * 4U)

/**
 * @brief Encode the FFT length as the base-2 logarithm.
 *
 * This function calculates the base-2 logarithm of the given FFT length.
 * For example, if the input length is 8, the output encoding will be 3,
 * because 2^3 = 8.
 * 
 * Can be used to verify that the configured FFT sizes in
 * MD_RD_CTRL.RFFT_SIZE match the expected radar geometry.
 *
 * @param length The FFT length to encode.
 * @return The base-2 logarithm of the FFT length.
 */
static uint32_t fft_size_encoding(uint32_t length)
{
    uint32_t encoding = 0U;

    while (length > 1U)
    {
        length >>= 1U;
        encoding++;
    }

    return encoding;
}


void dsp_chain_describe(uint32_t stages,
                        uint32_t num_rx,
                        uint32_t num_chirps,
                        uint32_t num_samples_per_chirp,
                        dsp_chain_t* chain)
{
    if (chain == NULL)
    {
        return;
    }

    chain->forwarding_mask = 0U;
    chain->adc_length_bytes = 0U;
    chain->rfft_length_bytes = 0U;
    chain->dfft_length_bytes = 0U;

    if ((stages & DSP_STAGE_ADC) != 0U)
    {
        chain->forwarding_mask |= BGT60CUTR13B_MD_RD_FIFO_CTRL_AVG_EN_msk;
        chain->adc_length_bytes =
            WORDS_TO_BYTES(DSP_CHAIN_ADC_WORDS(num_rx, num_chirps, num_samples_per_chirp));
    }

    if ((stages & DSP_STAGE_RFFT) != 0U)
    {
        chain->forwarding_mask |= BGT60CUTR13B_MD_RD_FIFO_CTRL_RFFT_EN_msk;
        chain->rfft_length_bytes =
            WORDS_TO_BYTES(DSP_CHAIN_RFFT_WORDS(num_rx, num_chirps, num_samples_per_chirp));
    }

    if ((stages & DSP_STAGE_DFFT) != 0U)
    {
        chain->forwarding_mask |= BGT60CUTR13B_MD_RD_FIFO_CTRL_CFFT_EN_msk;
        chain->dfft_length_bytes =
            WORDS_TO_BYTES(DSP_CHAIN_DFFT_WORDS(num_rx, num_chirps, num_samples_per_chirp));
    }

    chain->frame_words = DSP_CHAIN_FRAME_WORDS(stages, num_rx, num_chirps, num_samples_per_chirp);
}


bool dsp_chain_fft_sizes_match(uint32_t md_rd_ctrl, uint32_t num_chirps, uint32_t num_samples_per_chirp)
{
    uint32_t rfft_size =
        (md_rd_ctrl & BGT60CUTR13B_MD_RD_CTRL_RFFT_SIZE_msk) >> BGT60CUTR13B_MD_RD_CTRL_RFFT_SIZE_pos;
    uint32_t cfft_size =
        (md_rd_ctrl & BGT60CUTR13B_MD_RD_CTRL_CFFT_SIZE_msk) >> BGT60CUTR13B_MD_RD_CTRL_CFFT_SIZE_pos;

    return (rfft_size == fft_size_encoding(num_samples_per_chirp)) &&
           (cfft_size == fft_size_encoding(num_chirps));
}
