/**
 * @file vrm_emotion_timeline.c
 * @brief Cloud timeIndex-driven facial emotion cues synced to TTS playback
 * @version 0.1
 * @date 2026-06-30
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_emotion_timeline.h"
#include "vrm_renderer.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define EMO_TIMELINE_MAX_SEGMENTS  64
#define EMO_NAME_MAX               32

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    uint32_t time_ms;
    float    intensity;
    char     name[EMO_NAME_MAX];
} emo_timeline_seg_t;

/* ---------------------------------------------------------------------------
 * File-scope variables
 * --------------------------------------------------------------------------- */
static emo_timeline_seg_t s_segs[EMO_TIMELINE_MAX_SEGMENTS];
static int                s_seg_count;
static int                s_timeline_active;
static uint32_t           s_stream_ms;
static int                s_last_applied;

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Clear all emotion segments and disable timeline mode.
 * @return none
 */
void vrm_emotion_timeline_reset(void)
{
    s_seg_count       = 0;
    s_timeline_active = 0;
    s_stream_ms       = 0;
    s_last_applied    = -1;
}

/**
 * @brief Append a timed emotion cue.
 * @param[in] name       VRM emotion preset name
 * @param[in] time_ms    Milliseconds from TTS stream start
 * @param[in] intensity  Expression strength 0.0–1.0
 * @return none
 */
void vrm_emotion_timeline_append(const char *name, uint32_t time_ms, float intensity)
{
    if (!name || name[0] == '\0') {
        return;
    }

    if (time_ms > 0) {
        s_timeline_active = 1;
    }

    if (s_seg_count >= EMO_TIMELINE_MAX_SEGMENTS) {
        return;
    }

    if (intensity < 0.0f) {
        intensity = 0.0f;
    }
    if (intensity > 1.0f) {
        intensity = 1.0f;
    }

    strncpy(s_segs[s_seg_count].name, name, EMO_NAME_MAX - 1);
    s_segs[s_seg_count].name[EMO_NAME_MAX - 1] = '\0';
    s_segs[s_seg_count].time_ms   = time_ms;
    s_segs[s_seg_count].intensity = intensity;
    s_seg_count++;
}

/**
 * @brief Update current TTS stream position.
 * @param[in] stream_ms Elapsed milliseconds since TTS PCM stream started
 * @return none
 */
void vrm_emotion_timeline_set_stream_ms(uint32_t stream_ms)
{
    s_stream_ms = stream_ms;
}

/**
 * @brief Apply emotion segment(s) due at the current stream position.
 * @return none
 */
void vrm_emotion_timeline_tick(void)
{
    int best = -1;

    if (!s_timeline_active || s_seg_count <= 0) {
        return;
    }

    for (int i = 0; i < s_seg_count; i++) {
        if (s_segs[i].time_ms <= s_stream_ms) {
            if (best < 0 || s_segs[i].time_ms >= s_segs[best].time_ms) {
                best = i;
            }
        }
    }

    if (best < 0 || best == s_last_applied) {
        return;
    }

    s_last_applied = best;
    vrm_viewer_set_emotion(s_segs[best].name, s_segs[best].intensity, 0.0f);
}

/**
 * @brief Return whether any segment used a non-zero timeIndex.
 * @return 1 if timeline mode is active, 0 otherwise
 */
int vrm_emotion_timeline_is_active(void)
{
    return s_timeline_active;
}
