/**
 * @file lip_sync.c
 * @brief Audio-driven lip sync — PCM energy + spectral-split analysis.
 *
 * See lip_sync.h for the algorithm description.
 */

#include "vrm_lip_sync.h"

#include <math.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Tuning — balanced preset (between subtle and exaggerated)           */
/* ------------------------------------------------------------------ */

#define LP_CUTOFF_HZ              600.0f

#define ENERGY_TAU_S              0.048f
#define WEIGHT_TAU_S              0.038f

#define ENERGY_NORM               3600.0f
#define ENERGY_FLOOR              100.0f

#define LIP_SYNC_OUTPUT_GAIN      0.58f
#define LIP_SYNC_MAX_WEIGHT       0.68f
#define LIP_SYNC_JAW_GAIN         0.72f

/*
 * Fallback delay before the first playback chunk (ms) if live delay is unknown.
 * Replaced automatically once audio starts and snd_pcm_delay is available.
 */
#define LIP_SYNC_FALLBACK_DELAY_MS  80

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline float __clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float __exp_lerp(float current, float target, float factor)
{
    return current + factor * (target - current);
}

static inline float __scale_out(float w)
{
    return __clampf(w * LIP_SYNC_OUTPUT_GAIN, 0.0f, LIP_SYNC_MAX_WEIGHT);
}

static void __hist_push(lip_sync_ctx_t *ctx, uint32_t end_sample)
{
    int idx = ctx->hist_head % LIP_SYNC_HISTORY_LEN;

    ctx->hist[idx].w_aa        = ctx->w_aa;
    ctx->hist[idx].w_ih        = ctx->w_ih;
    ctx->hist[idx].w_ou        = ctx->w_ou;
    ctx->hist[idx].w_ee        = ctx->w_ee;
    ctx->hist[idx].w_oh        = ctx->w_oh;
    ctx->hist[idx].eff_open    = ctx->smooth_energy;
    ctx->hist[idx].end_sample  = end_sample;

    ctx->hist_head = (ctx->hist_head + 1) % LIP_SYNC_HISTORY_LEN;
    if (ctx->hist_count < LIP_SYNC_HISTORY_LEN) {
        ctx->hist_count++;
    }
}

static const lip_sync_hist_t *__hist_lookup(const lip_sync_ctx_t *ctx)
{
    if (ctx->hist_count <= 0) {
        return NULL;
    }

    uint32_t delay_samples = ctx->live_delay_samples;
    if (delay_samples == 0) {
        delay_samples =
            (uint32_t)((int64_t)ctx->delay_ms * ctx->sample_rate / 1000);
    }

    uint32_t target = (ctx->total_samples > delay_samples)
                    ? (ctx->total_samples - delay_samples) : 0;

    int best_i   = -1;
    uint32_t best_end = 0;

    for (int i = 0; i < ctx->hist_count; i++) {
        int ri = (ctx->hist_head - 1 - i + LIP_SYNC_HISTORY_LEN) % LIP_SYNC_HISTORY_LEN;
        uint32_t end = ctx->hist[ri].end_sample;
        if (end <= target && end >= best_end) {
            best_end = end;
            best_i   = ri;
        }
    }

    if (best_i < 0) {
        best_i = (ctx->hist_head - ctx->hist_count + LIP_SYNC_HISTORY_LEN)
               % LIP_SYNC_HISTORY_LEN;
    }

    return &ctx->hist[best_i];
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void lip_sync_init(lip_sync_ctx_t *ctx, int sample_rate)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->sample_rate = (sample_rate > 0) ? sample_rate : 16000;
    ctx->delay_ms    = LIP_SYNC_FALLBACK_DELAY_MS;
}

void lip_sync_set_playback_delay_frames(lip_sync_ctx_t *ctx, uint32_t delay_frames)
{
    if (!ctx) {
        return;
    }

    ctx->live_delay_samples = delay_frames;
    if (ctx->sample_rate > 0 && delay_frames > 0) {
        ctx->delay_ms = (int)((delay_frames * 1000U) / (uint32_t)ctx->sample_rate);
        if (ctx->delay_ms < 20) {
            ctx->delay_ms = 20;
        }
    }
}

void lip_sync_feed_pcm(lip_sync_ctx_t *ctx,
                       const void *buf, uint32_t byte_len,
                       int channels, int bits)
{
    if (!ctx || !buf || byte_len == 0) {
        return;
    }

    if (bits != 16) {
        return;
    }

    const int16_t *pcm = (const int16_t *)buf;
    uint32_t total_samples = byte_len / sizeof(int16_t);
    if (total_samples == 0) {
        return;
    }

    uint32_t frames = total_samples / (uint32_t)channels;
    if (frames == 0) {
        return;
    }

    const float alpha =
        1.0f - expf(-6.28318f * LP_CUTOFF_HZ / (float)ctx->sample_rate);

    double sum_sq_total = 0.0;
    double sum_sq_lp    = 0.0;
    float  lp_state     = ctx->lp_state;

    for (uint32_t f = 0; f < frames; f++) {
        float mono;
        if (channels == 1) {
            mono = (float)pcm[f];
        } else {
            mono = ((float)pcm[f * 2] + (float)pcm[f * 2 + 1]) * 0.5f;
        }

        lp_state = alpha * mono + (1.0f - alpha) * lp_state;

        sum_sq_total += (double)mono * (double)mono;
        sum_sq_lp    += (double)lp_state * (double)lp_state;
    }

    ctx->lp_state = lp_state;

    float rms_total = sqrtf((float)(sum_sq_total / frames));
    float rms_lp    = sqrtf((float)(sum_sq_lp / frames));

    float rms_sq_hp = rms_total * rms_total - rms_lp * rms_lp;
    float rms_hp    = (rms_sq_hp > 0.0f) ? sqrtf(rms_sq_hp) : 0.0f;

    float openness = 0.0f;
    if (rms_total > ENERGY_FLOOR) {
        openness = __clampf(rms_total / ENERGY_NORM, 0.0f, 1.0f);
    }

    float block_dt  = (float)frames / (float)ctx->sample_rate;
    float e_factor  = 1.0f - expf(-block_dt / ENERGY_TAU_S);
    ctx->smooth_energy = __exp_lerp(ctx->smooth_energy, openness, e_factor);
    float eff_open = ctx->smooth_energy;

    float hp_ratio = 0.0f;
    if (rms_total > ENERGY_FLOOR) {
        hp_ratio = __clampf(rms_hp / rms_total, 0.0f, 1.0f);
    }

    /* Moderate vowel mix — visible motion without full five-shape stack. */
    float taa = eff_open * 0.42f * (1.0f - 0.7f * hp_ratio);
    float tih = eff_open * 0.30f * hp_ratio;
    float tou = eff_open * 0.26f * (1.0f - hp_ratio);
    float tee = eff_open * 0.24f * hp_ratio;
    float toh = eff_open * 0.28f * (1.0f - hp_ratio);

    ctx->target_aa = __clampf(taa, 0.0f, 1.0f);
    ctx->target_ih = __clampf(tih, 0.0f, 1.0f);
    ctx->target_ou = __clampf(tou, 0.0f, 1.0f);
    ctx->target_ee = __clampf(tee, 0.0f, 1.0f);
    ctx->target_oh = __clampf(toh, 0.0f, 1.0f);

    float w_factor = 1.0f - expf(-block_dt / WEIGHT_TAU_S);

    ctx->w_aa = __exp_lerp(ctx->w_aa, ctx->target_aa, w_factor);
    ctx->w_ih = __exp_lerp(ctx->w_ih, ctx->target_ih, w_factor);
    ctx->w_ou = __exp_lerp(ctx->w_ou, ctx->target_ou, w_factor);
    ctx->w_ee = __exp_lerp(ctx->w_ee, ctx->target_ee, w_factor);
    ctx->w_oh = __exp_lerp(ctx->w_oh, ctx->target_oh, w_factor);

    ctx->total_samples += frames;
    __hist_push(ctx, ctx->total_samples);
}

void lip_sync_get_weights(const lip_sync_ctx_t *ctx,
                          float *aa, float *ih, float *ou,
                          float *ee, float *oh)
{
    if (!ctx) {
        return;
    }

    const lip_sync_hist_t *snap = __hist_lookup(ctx);
    if (!snap) {
        if (aa) *aa = 0.0f;
        if (ih) *ih = 0.0f;
        if (ou) *ou = 0.0f;
        if (ee) *ee = 0.0f;
        if (oh) *oh = 0.0f;
        return;
    }

    if (aa) *aa = __scale_out(snap->w_aa);
    if (ih) *ih = __scale_out(snap->w_ih);
    if (ou) *ou = __scale_out(snap->w_ou);
    if (ee) *ee = __scale_out(snap->w_ee);
    if (oh) *oh = __scale_out(snap->w_oh);
}

float lip_sync_get_jaw_weight(const lip_sync_ctx_t *ctx)
{
    if (!ctx) {
        return 0.0f;
    }

    const lip_sync_hist_t *snap = __hist_lookup(ctx);
    if (!snap) {
        return 0.0f;
    }

    /* Jaw follows overall loudness — clearer than stacking vowel peaks. */
    return __scale_out(snap->eff_open * LIP_SYNC_JAW_GAIN);
}

void lip_sync_reset(lip_sync_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    ctx->lp_state      = 0.0f;
    ctx->smooth_energy = 0.0f;
    ctx->target_aa = ctx->target_ih = ctx->target_ou =
    ctx->target_ee = ctx->target_oh = 0.0f;
    ctx->w_aa = ctx->w_ih = ctx->w_ou = ctx->w_ee = ctx->w_oh = 0.0f;
    ctx->hist_head     = 0;
    ctx->hist_count    = 0;
    ctx->total_samples = 0;
    ctx->live_delay_samples = 0;
    memset(ctx->hist, 0, sizeof(ctx->hist));
}
