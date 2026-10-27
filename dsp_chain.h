/******************************************************************************
 * File Name: dsp_chain.h
 *
 * Description: Selection of the sensor processing chain stages that are streamed to the host.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2026-YEAR Cypress Semiconductor $
 *******************************************************************************/

#ifndef DSP_CHAIN_H_
#define DSP_CHAIN_H_

#include <stdbool.h>
#include <stdint.h>

/** Averaged ADC samples, the input the range FFT is computed from */
#define DSP_STAGE_ADC  (1U << 0)

/** Output of the on-chip range FFT */
#define DSP_STAGE_RFFT (1U << 1)

/** Output of the on-chip Doppler FFT */
#define DSP_STAGE_DFFT (1U << 2)

/** A forwarded processing stage is always 32 bits wide, so every averaged sample costs one
 * FIFO word; only the unprocessed ADC path packs two samples into a word. */
#define DSP_CHAIN_ADC_WORDS(num_rx, num_chirps, num_samples_per_chirp) \
    ((num_rx) * (num_chirps) * (num_samples_per_chirp))

/** A real FFT of N real samples has N/2 distinct complex bins; the rest is its mirror image.
 * Each bin occupies two FIFO words, one for its real and one for its imaginary part. */
#define DSP_CHAIN_RFFT_WORDS(num_rx, num_chirps, num_samples_per_chirp) \
    ((num_rx) * ((num_samples_per_chirp) / 2U) * (num_chirps) * 2U)

/** One complex bin per range and Doppler bin; the Doppler FFT runs over the chirps of a frame. */
#define DSP_CHAIN_DFFT_WORDS(num_rx, num_chirps, num_samples_per_chirp) \
    ((num_rx) * ((num_samples_per_chirp) / 2U) * (num_chirps) * 2U)

/** FIFO words of one radar frame, all selected sections together. */
#define DSP_CHAIN_FRAME_WORDS(stages, num_rx, num_chirps, num_samples_per_chirp)                   \
    (((((stages) & DSP_STAGE_ADC) != 0U) ?                                                         \
      DSP_CHAIN_ADC_WORDS(num_rx, num_chirps, num_samples_per_chirp) : 0U) +                       \
     ((((stages) & DSP_STAGE_RFFT) != 0U) ?                                                        \
      DSP_CHAIN_RFFT_WORDS(num_rx, num_chirps, num_samples_per_chirp) : 0U) +                      \
     ((((stages) & DSP_STAGE_DFFT) != 0U) ?                                                        \
      DSP_CHAIN_DFFT_WORDS(num_rx, num_chirps, num_samples_per_chirp) : 0U))

/** True for a selection the host cannot split into sections.
 *
 * The sensor forwards a stage as soon as it has completed it: the averaged ADC samples and the
 * range FFT are both produced per burst and therefore alternate inside one radar frame, while the
 * Doppler FFT runs once the frame is complete and stays one contiguous block at the end.
 */
#define DSP_CHAIN_STAGES_INTERLEAVE(stages) \
    ((((stages) & DSP_STAGE_ADC) != 0U) && (((stages) & DSP_STAGE_RFFT) != 0U))

/** Everything about one radar frame that follows from the selected stages */
typedef struct
{
    uint32_t forwarding_mask;    /**< Stages to forward, as written to MD_RD_FIFO_CTRL */
    uint32_t frame_words;        /**< FIFO words of one radar frame, all sections together */
    uint32_t adc_length_bytes;   /**< Size of the raw ADC section, 0 if not selected */
    uint32_t rfft_length_bytes;  /**< Size of the range FFT section, 0 if not selected */
    uint32_t dfft_length_bytes;  /**< Size of the Doppler FFT section, 0 if not selected */
} dsp_chain_t;

/**
 * @brief Derives the frame layout and the forwarding register value from the selected stages.
 *
 * The section sizes are reported to the host in the order the sensor writes them, so a section
 * that is not selected is reported as 0 rather than left out.
 *
 * @param stages Bitwise OR of the DSP_STAGE_* flags to stream.
 * @param num_rx Number of enabled receive antennas.
 * @param num_chirps Number of chirps per radar frame.
 * @param num_samples_per_chirp Number of samples per chirp.
 * @param chain Layout to fill.
 */
void dsp_chain_describe(uint32_t stages,
                        uint32_t num_rx,
                        uint32_t num_chirps,
                        uint32_t num_samples_per_chirp,
                        dsp_chain_t* chain);

/**
 * @brief Checks the FFT sizes the sensor is configured for against the expected geometry.
 *
 * The section sizes are derived from the acquisition parameters of the configuration header. A
 * register configuration that disagreed about an FFT size would reshape every frame the host
 * decodes without anything looking wrong, so the sizes are verified rather than assumed.
 *
 * @param md_rd_ctrl Value read from the MD_RD_CTRL register.
 * @param num_chirps Number of chirps per radar frame, which is the Doppler FFT length.
 * @param num_samples_per_chirp Number of samples per chirp, which is the range FFT length.
 * @return true when both FFT sizes match the expected geometry.
 */
bool dsp_chain_fft_sizes_match(uint32_t md_rd_ctrl, uint32_t num_chirps, uint32_t num_samples_per_chirp);

#endif // ifndef DSP_CHAIN_H_
