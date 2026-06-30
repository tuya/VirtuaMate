/**
 * @file vrm_text_timeline.h
 * @brief Cloud timeIndex-driven subtitle reveal synced to TTS audio stream position.
 * @version 0.1
 * @date 2025-06-30
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef VRM_TEXT_TIMELINE_H
#define VRM_TEXT_TIMELINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clear all segments and disable timeline mode.
 * @return none
 */
void vrm_text_timeline_reset(void);

/**
 * @brief Append a text chunk with cloud timeIndex (milliseconds from TTS start).
 * @param[in] text     UTF-8 fragment (may be NULL if len is 0).
 * @param[in] len      Byte length of text.
 * @param[in] time_ms  Cloud timeIndex for when this fragment should appear.
 * @return none
 */
void vrm_text_timeline_append(const char *text, uint16_t len, uint32_t time_ms);

/**
 * @brief Update current position on the TTS audio stream timeline.
 * @param[in] stream_ms Elapsed milliseconds since TTS PCM stream started.
 * @return none
 */
void vrm_text_timeline_set_stream_ms(uint32_t stream_ms);

/**
 * @brief Return whether any segment carried a non-zero timeIndex.
 * @return 1 if timeline mode is active, 0 for legacy full-subtitle mode.
 */
int vrm_text_timeline_is_active(void);

/**
 * @brief Build the subtitle visible at the current stream position.
 * @param[out] out     Output buffer.
 * @param[in]  out_len Size of out in bytes.
 * @return none
 */
void vrm_text_timeline_build_visible(char *out, uint32_t out_len);

/**
 * @brief Estimated end of speech on the stream timeline (last segment time + tail).
 * @return Milliseconds from TTS stream start, or 0 if empty.
 */
uint32_t vrm_text_timeline_speech_end_ms(void);

/**
 * @brief Return current stream position last set by set_stream_ms().
 * @return Stream position in milliseconds.
 */
uint32_t vrm_text_timeline_get_stream_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* VRM_TEXT_TIMELINE_H */
