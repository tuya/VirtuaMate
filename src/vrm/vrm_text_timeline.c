/**
 * @file vrm_text_timeline.c
 * @brief Cloud timeIndex-driven subtitle reveal synced to TTS audio stream position.
 * @version 0.1
 * @date 2025-06-30
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_text_timeline.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define TIMELINE_MAX_SEGMENTS  128
#define TIMELINE_TEXT_MAX      (16 * 1024)
#define TIMELINE_SPEECH_TAIL_MS  400U

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    uint32_t time_ms;
    uint16_t off;
    uint16_t len;
} timeline_seg_t;

/* ---------------------------------------------------------------------------
 * File-scope variables
 * --------------------------------------------------------------------------- */
static char            s_blob[TIMELINE_TEXT_MAX];
static uint32_t        s_blob_len;
static timeline_seg_t  s_segs[TIMELINE_MAX_SEGMENTS];
static int             s_seg_count;
static int             s_timeline_active;
static uint32_t        s_stream_ms;
static uint32_t        s_last_seg_time_ms;

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Clear all segments and disable timeline mode.
 * @return none
 */
void vrm_text_timeline_reset(void)
{
    s_blob_len          = 0;
    s_blob[0]           = '\0';
    s_seg_count         = 0;
    s_timeline_active   = 0;
    s_stream_ms         = 0;
    s_last_seg_time_ms  = 0;
}

/**
 * @brief Append a text chunk with cloud timeIndex (milliseconds from TTS start).
 * @param[in] text     UTF-8 fragment (may be NULL if len is 0).
 * @param[in] len      Byte length of text.
 * @param[in] time_ms  Cloud timeIndex for when this fragment should appear.
 * @return none
 */
void vrm_text_timeline_append(const char *text, uint16_t len, uint32_t time_ms)
{
    if (len > 0 && text == NULL) {
        return;
    }

    if (time_ms > 0) {
        s_timeline_active = 1;
    }

    if (len == 0) {
        if (time_ms > s_last_seg_time_ms) {
            s_last_seg_time_ms = time_ms;
        }
        return;
    }

    if (s_blob_len + (uint32_t)len >= TIMELINE_TEXT_MAX) {
        return;
    }

    if (s_seg_count >= TIMELINE_MAX_SEGMENTS) {
        return;
    }

    memcpy(s_blob + s_blob_len, text, len);
    s_blob[s_blob_len + len] = '\0';

    s_segs[s_seg_count].time_ms = time_ms;
    s_segs[s_seg_count].off     = (uint16_t)s_blob_len;
    s_segs[s_seg_count].len     = len;
    s_seg_count++;

    s_blob_len += (uint32_t)len;
    if (time_ms > s_last_seg_time_ms) {
        s_last_seg_time_ms = time_ms;
    }
}

/**
 * @brief Update current position on the TTS audio stream timeline.
 * @param[in] stream_ms Elapsed milliseconds since TTS PCM stream started.
 * @return none
 */
void vrm_text_timeline_set_stream_ms(uint32_t stream_ms)
{
    s_stream_ms = stream_ms;
}

/**
 * @brief Return whether any segment carried a non-zero timeIndex.
 * @return 1 if timeline mode is active, 0 for legacy full-subtitle mode.
 */
int vrm_text_timeline_is_active(void)
{
    return s_timeline_active;
}

/**
 * @brief Build the subtitle visible at the current stream position.
 * @param[out] out     Output buffer.
 * @param[in]  out_len Size of out in bytes.
 * @return none
 */
void vrm_text_timeline_build_visible(char *out, uint32_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }

    out[0] = '\0';

    if (!s_timeline_active || s_seg_count <= 0) {
        if (s_blob_len > 0) {
            uint32_t copy_len = s_blob_len;
            if (copy_len >= out_len) {
                copy_len = out_len - 1U;
            }
            memcpy(out, s_blob, copy_len);
            out[copy_len] = '\0';
        }
        return;
    }

    uint32_t pos = 0;
    for (int i = 0; i < s_seg_count; i++) {
        if (s_segs[i].time_ms > s_stream_ms) {
            break;
        }

        uint32_t seg_end = pos + (uint32_t)s_segs[i].len;
        if (seg_end >= out_len) {
            uint32_t room = out_len - pos - 1U;
            if (pos + room > out_len - 1U) {
                break;
            }
            memcpy(out + pos, s_blob + s_segs[i].off, room);
            pos += room;
            break;
        }

        memcpy(out + pos, s_blob + s_segs[i].off, s_segs[i].len);
        pos += (uint32_t)s_segs[i].len;
    }

    out[pos] = '\0';
}

/**
 * @brief Estimated end of speech on the stream timeline (last segment time + tail).
 * @return Milliseconds from TTS stream start, or 0 if empty.
 */
uint32_t vrm_text_timeline_speech_end_ms(void)
{
    if (s_seg_count <= 0) {
        return 0;
    }

    return s_last_seg_time_ms + TIMELINE_SPEECH_TAIL_MS;
}

/**
 * @brief Return current stream position last set by set_stream_ms().
 * @return Stream position in milliseconds.
 */
uint32_t vrm_text_timeline_get_stream_ms(void)
{
    return s_stream_ms;
}
