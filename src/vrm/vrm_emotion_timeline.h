/**
 * @file vrm_emotion_timeline.h
 * @brief Cloud timeIndex-driven facial emotion cues synced to TTS playback
 * @version 0.1
 * @date 2026-06-30
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef VRM_EMOTION_TIMELINE_H
#define VRM_EMOTION_TIMELINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clear all emotion segments and disable timeline mode.
 * @return none
 */
void vrm_emotion_timeline_reset(void);

/**
 * @brief Append a timed emotion cue.
 * @param[in] name       VRM emotion preset name
 * @param[in] time_ms    Milliseconds from TTS stream start
 * @param[in] intensity  Expression strength 0.0–1.0
 * @return none
 */
void vrm_emotion_timeline_append(const char *name, uint32_t time_ms, float intensity);

/**
 * @brief Update current TTS stream position.
 * @param[in] stream_ms Elapsed milliseconds since TTS PCM stream started
 * @return none
 */
void vrm_emotion_timeline_set_stream_ms(uint32_t stream_ms);

/**
 * @brief Apply emotion segment(s) due at the current stream position.
 * @return none
 */
void vrm_emotion_timeline_tick(void);

/**
 * @brief Return whether any segment used a non-zero timeIndex.
 * @return 1 if timeline mode is active, 0 otherwise
 */
int vrm_emotion_timeline_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* VRM_EMOTION_TIMELINE_H */
