/**
 * @file vrm_lip_sync.h
 * @brief Audio-driven lip sync: feeds decoded PCM samples and produces per-frame
 *        vowel weights (aa / ih / ou / ee / oh) for VRM expression blending.
 *
 * Thread model:
 *   - Producer (audio output thread): calls lip_sync_feed_pcm()
 *   - Consumer (render/main thread):  calls lip_sync_get_weights()
 *
 * Playback delay compensation:
 *   PCM is analysed when written to the audio driver, but sound is heard later.
 *   During playback the driver reports live buffer delay (ALSA snd_pcm_delay);
 *   before playback starts we fall back to buffer+period from driver config.
 *   No fixed millisecond constant is required across boards or buffer sizes.
 *
 * @version 1.1
 * @date 2025-06-30
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#ifndef VRM_LIP_SYNC_H
#define VRM_LIP_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIP_SYNC_HISTORY_LEN 48

typedef struct {
    float    w_aa;
    float    w_ih;
    float    w_ou;
    float    w_ee;
    float    w_oh;
    float    eff_open;
    uint32_t end_sample;
} lip_sync_hist_t;

typedef struct {
    float lp_state;
    float smooth_energy;
    float target_aa;
    float target_ih;
    float target_ou;
    float target_ee;
    float target_oh;
    volatile float w_aa;
    volatile float w_ih;
    volatile float w_ou;
    volatile float w_ee;
    volatile float w_oh;
    lip_sync_hist_t hist[LIP_SYNC_HISTORY_LEN];
    int             hist_head;
    int             hist_count;
    uint32_t        total_samples;
    int             sample_rate;
    int             delay_ms;
    uint32_t        live_delay_samples;
} lip_sync_ctx_t;

/**
 * @brief Initialise the context. Call once before feeding audio.
 * @param[in] ctx         Context to initialise.
 * @param[in] sample_rate PCM sample rate in Hz (typically 16000).
 * @return none
 */
void lip_sync_init(lip_sync_ctx_t *ctx, int sample_rate);

/**
 * @brief Update live playback delay used for lip-sync history lookup.
 * @param[in] ctx          Lip sync context.
 * @param[in] delay_frames Delay in PCM frames (from tdl_audio_get_playback_delay_frames).
 * @return none
 */
void lip_sync_set_playback_delay_frames(lip_sync_ctx_t *ctx, uint32_t delay_frames);

/**
 * @brief Feed a block of decoded PCM to update vowel weights.
 * @param[in] ctx      Lip sync context.
 * @param[in] buf      Pointer to raw PCM bytes.
 * @param[in] byte_len Number of bytes in buf.
 * @param[in] channels 1 = mono, 2 = stereo (stereo is downmixed by averaging).
 * @param[in] bits     Bits per sample (only 16 is supported; others are ignored).
 * @return none
 */
void lip_sync_feed_pcm(lip_sync_ctx_t *ctx,
                       const void *buf, uint32_t byte_len,
                       int channels, int bits);

/**
 * @brief Read delayed, gain-scaled vowel weights. Call from the render thread.
 * @param[in]  ctx  Lip sync context.
 * @param[out] aa   Mouth open wide weight (optional).
 * @param[out] ih   Mouth narrow weight (optional).
 * @param[out] ou   Mouth round weight (optional).
 * @param[out] ee   Mouth half-open weight (optional).
 * @param[out] oh   Mouth round-open weight (optional).
 * @return none
 */
void lip_sync_get_weights(const lip_sync_ctx_t *ctx,
                          float *aa, float *ih, float *ou,
                          float *ee, float *oh);

/**
 * @brief Read delayed jaw-open weight derived from overall mouth energy.
 * @param[in] ctx Lip sync context.
 * @return Jaw weight in [0, LIP_SYNC_MAX_WEIGHT].
 */
float lip_sync_get_jaw_weight(const lip_sync_ctx_t *ctx);

/**
 * @brief Reset all weights to zero (e.g., when speaking stops).
 * @param[in] ctx  Lip sync context.
 * @return none
 */
void lip_sync_reset(lip_sync_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VRM_LIP_SYNC_H */
