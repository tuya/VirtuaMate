/**
 * @file ai_skill.c
 * @brief AI skill module (VirtuaMate overlay)
 *
 * NLG protocol extensions (per chunk or batch):
 *   - "emotion": VRM preset name (e.g. "happy")
 *   - "emotionIndex": ms from TTS start (defaults to "timeIndex")
 *   - "emotionIntensity": 0.0–1.0 (optional, default 1.0)
 *   - "emotionTimeline": [{ "emotion", "timeIndex", "intensity"? }, ...]
 *   - "tags": emoji code "U+1F642" with "timeIndex" schedules timed emotion
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */
#include "tal_api.h"
#include "cJSON.h"
#include "mix_method.h"

#include "ai_user_event.h"
#include "skill_emotion.h"

#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
#include "ai_audio_player.h"
#include "skill_music_story.h"
#endif

#if defined(ENABLE_COMP_AI_PICTURE) && (ENABLE_COMP_AI_PICTURE == 1)
#include "ai_picture_output.h"
#endif

#include "ai_skill.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define NLG_CONTENT_BUF_SZ 2048

/* ---------------------------------------------------------------------------
 * File-scope variables
 * --------------------------------------------------------------------------- */
static bool s_nlg_in_stream = false;
static char s_nlg_content_buf[NLG_CONTENT_BUF_SZ];

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

/**
 * @brief Read a JSON string field safely.
 * @param[in] item cJSON item
 * @return String value or NULL
 */
static const char *__json_get_string(const cJSON *item)
{
    if (item == NULL || !cJSON_IsString(item) || item->valuestring == NULL) {
        return NULL;
    }

    return item->valuestring;
}

/**
 * @brief Tuya NLG emotion tag Unicode codes (see skill_emotion.c).
 */
static const char *const s_tuya_emoji_codes[] = {
    "U+1F636", "U+1F642", "U+1F606", "U+1F602", "U+1F614", "U+1F620", "U+1F62D",
    "U+1F60D", "U+1F633", "U+1F62F", "U+1F631", "U+1F914", "U+1F609", "U+1F60E",
    "U+1F60C", "U+1F924", "U+1F618", "U+1F60F", "U+1F634", "U+1F61C", "U+1F644",
};

/**
 * @brief Return byte length if [in] s starts with a known Tuya emoji UTF-8 sequence.
 * @param[in] s UTF-8 text
 * @return Prefix length in bytes, or 0 if none
 */
static size_t __tuya_emoji_prefix_len(const char *s)
{
    char utf8[8];
    size_t i;

    if (!s || s[0] == '\0') {
        return 0;
    }

    for (i = 0; i < CNTSOF(s_tuya_emoji_codes); i++) {
        int n = ai_emoji_unicode_to_utf8(s_tuya_emoji_codes[i], utf8, sizeof(utf8));
        if (n > 0 && strncmp(s, utf8, (size_t)n) == 0) {
            return (size_t)n;
        }
    }

    return 0;
}

/**
 * @brief Strip leading Tuya emoji from NLG content for TTS/IM/subtitle display.
 * @param[in] root NLG JSON root (may contain "tags")
 * @return Display-safe content (may alias input or use s_nlg_content_buf)
 */
static const char *__nlg_content_strip_emoji(cJSON *root)
{
    const char *content = __json_get_string(cJSON_GetObjectItem(root, "content"));
    const char *src;
    size_t skip = 0;

    if (!content) {
        return "";
    }

    src = content;

    {
        cJSON *tags = cJSON_GetObjectItem(root, "tags");
        if (tags && cJSON_IsArray(tags) && cJSON_GetArraySize(tags) > 0) {
            const char *tag = __json_get_string(cJSON_GetArrayItem(tags, 0));
            char utf8[8];

            if (tag) {
                int n = ai_emoji_unicode_to_utf8(tag, utf8, sizeof(utf8));
                if (n > 0 && strncmp(src, utf8, (size_t)n) == 0) {
                    skip = (size_t)n;
                }
            }
        }
    }

    if (skip == 0) {
        skip = __tuya_emoji_prefix_len(src);
    }

    if (skip == 0) {
        return content;
    }

    src += skip;
    snprintf(s_nlg_content_buf, sizeof(s_nlg_content_buf), "%s", src);
    return s_nlg_content_buf;
}

/**
 * @brief Emit immediate or time-scheduled emotion to the app layer.
 * @param[in] name       VRM emotion preset name
 * @param[in] time_ms    TTS offset in ms; 0 = apply immediately
 * @param[in] intensity  Expression strength 0.0–1.0
 * @return none
 */
static void __emit_emotion_cue(const char *name, uint32_t time_ms, float intensity)
{
    AI_NOTIFY_EMO_TIMELINE_T timeline;
    AI_AGENT_EMO_T emo;

    if (!name || name[0] == '\0') {
        return;
    }

    if (intensity < 0.0f) {
        intensity = 0.0f;
    }
    if (intensity > 1.0f) {
        intensity = 1.0f;
    }

    if (time_ms > 0) {
        timeline.name      = name;
        timeline.time_ms   = time_ms;
        timeline.intensity = intensity;
        ai_user_event_notify(AI_USER_EVT_EMOTION_TIMELINE, &timeline);
        PR_DEBUG("emotion timeline: %s @ %u ms (%.2f)", name, (unsigned)time_ms, intensity);
        return;
    }

    emo.emoji = NULL;
    emo.name  = name;
    ai_agent_play_emo(&emo);
}

/**
 * @brief Parse optional float intensity from a JSON object.
 * @param[in] obj cJSON object
 * @return Intensity in 0.0–1.0, default 1.0
 */
static float __json_get_intensity(const cJSON *obj)
{
    const cJSON *item = cJSON_GetObjectItem(obj, "intensity");

    if (!item) {
        item = cJSON_GetObjectItem(obj, "emotionIntensity");
    }

    if (item && cJSON_IsNumber(item)) {
        float v = (float)item->valuedouble;
        if (v < 0.0f) {
            return 0.0f;
        }
        if (v > 1.0f) {
            return 1.0f;
        }
        return v;
    }

    return 1.0f;
}

/**
 * @brief Parse timeIndex / emotionIndex from a JSON object.
 * @param[in] obj          cJSON object
 * @param[in] fallback_ms  Value when fields are absent
 * @return Milliseconds from TTS start
 */
static uint32_t __json_get_time_ms(const cJSON *obj, uint32_t fallback_ms)
{
    const cJSON *item = cJSON_GetObjectItem(obj, "emotionIndex");

    if (!item) {
        item = cJSON_GetObjectItem(obj, "timeIndex");
    }

    if (item && cJSON_IsNumber(item) && item->valueint >= 0) {
        return (uint32_t)item->valueint;
    }

    return fallback_ms;
}

/**
 * @brief Parse emotionTimeline array from NLG JSON.
 * @param[in] root NLG JSON root
 * @return none
 */
static void __parse_emotion_timeline_array(cJSON *root)
{
    cJSON *arr = cJSON_GetObjectItem(root, "emotionTimeline");

    if (!arr || !cJSON_IsArray(arr)) {
        return;
    }

    int count = cJSON_GetArraySize(arr);
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        const char *name = NULL;

        if (!item || !cJSON_IsObject(item)) {
            continue;
        }

        name = __json_get_string(cJSON_GetObjectItem(item, "emotion"));
        if (!name) {
            name = __json_get_string(cJSON_GetObjectItem(item, "name"));
        }

        if (!name) {
            continue;
        }

        __emit_emotion_cue(name,
                           __json_get_time_ms(item, 0),
                           __json_get_intensity(item));
    }
}

/**
 * @brief Parse per-chunk emotion fields and tags from NLG JSON.
 * @param[in] root       NLG JSON root
 * @param[in] time_index timeIndex from the same NLG chunk
 * @return none
 */
static void __parse_emotion_fields(cJSON *root, uint32_t time_index)
{
    const char *emotion = NULL;
    uint32_t emo_ms = 0;
    float intensity = 1.0f;

    __parse_emotion_timeline_array(root);

    emotion = __json_get_string(cJSON_GetObjectItem(root, "emotion"));
    if (emotion) {
        emo_ms = __json_get_time_ms(root, time_index);
        intensity = __json_get_intensity(root);
        __emit_emotion_cue(emotion, emo_ms, intensity);
    }

    {
        cJSON *tags_array = cJSON_GetObjectItem(root, "tags");
        if (tags_array && cJSON_IsArray(tags_array) && cJSON_GetArraySize(tags_array) > 0) {
            const char *emoji = __json_get_string(cJSON_GetArrayItem(tags_array, 0));
            const char *name = NULL;

            if (emoji && emoji[0] != '\0') {
                name = ai_agent_emoji_get_name(emoji);
                if (name) {
                    emo_ms = __json_get_time_ms(root, time_index);
                    __emit_emotion_cue(name, emo_ms, 1.0f);
                }
            }
        }
    }
}

/**
 * @brief Process AI skill data from JSON.
 * @param[in] root JSON root object containing skill data
 * @param[in] eof End of file flag
 * @return OPRT_OK on success
 */
static OPERATE_RET __ai_skills_process(cJSON *root, bool eof)
{
    OPERATE_RET rt = OPRT_OK;
    const cJSON *node = NULL;
    const char *code = NULL;

    node = cJSON_GetObjectItem(root, "code");
    code = __json_get_string(node);
    if (!code) {
        return OPRT_OK;
    }

    PR_NOTICE("text -> skill code: %s", code);
    if (strcmp(code, "emo") == 0 || strcmp(code, "llm_emo") == 0) {
        ai_skill_emo_process(root);
    }
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
    else if (strcmp(code, "music") == 0 || strcmp(code, "story") == 0) {
        AI_AUDIO_MUSIC_T *music = NULL;
        if (ai_skill_parse_music(root, &music) == OPRT_OK) {
            ai_skill_parse_music_dump(music);
            ai_audio_play_music(music);
            ai_skill_parse_music_free(music);
        }
    } else if (strcmp(code, "PlayControl") == 0) {
        AI_AUDIO_MUSIC_T *music = NULL;
        if ((rt = ai_skill_parse_playcontrol(root, &music)) == 0) {
            ai_skill_parse_music_dump(music);
            ai_skill_playcontrol_music(music);
            ai_skill_parse_music_free(music);
        }
    }
#endif
    else {
        PR_NOTICE("skill %s not handled", code);
        ai_user_event_notify(AI_USER_EVT_SKILL, root);
    }

    return rt;
}

/**
 * @brief Process ASR text stream.
 * @param[in] root JSON root
 * @param[in] eof End flag
 * @return OPRT_OK on success
 */
static OPERATE_RET __ai_asr_process(cJSON *root, BOOL_T eof)
{
    const char *content = __json_get_string(root);

    (void)eof;

    if (!content) {
        content = "";
    }
    PR_NOTICE("text -> ASR result: %s", content);

    s_nlg_in_stream = false;

    {
        AI_NOTIFY_TEXT_T text;
        text.data      = (char *)content;
        text.datalen   = strlen(content);
        text.timeindex = 0;
        ai_user_event_notify((0 == strlen(content)) ? AI_USER_EVT_ASR_EMPTY : AI_USER_EVT_ASR_OK, &text);
    }

    return OPRT_OK;
}

/**
 * @brief Process NLG text stream (VirtuaMate: emotion + timeIndex).
 * @param[in] root JSON root
 * @param[in] eof End flag
 * @return OPRT_OK on success
 */
static OPERATE_RET __ai_nlg_process(cJSON *root, bool eof)
{
    char *json_str = cJSON_PrintUnformatted(root);
    PR_NOTICE("json-str %s", json_str);
    cJSON_free(json_str);

    const char *content = __nlg_content_strip_emoji(root);

    AI_NOTIFY_TEXT_T text;
    text.data    = (char *)content;
    text.datalen = strlen(content);

    {
        cJSON *time_idx = cJSON_GetObjectItem(root, "timeIndex");
        text.timeindex = time_idx ? (uint32_t)time_idx->valueint : 0;
    }

    PR_NOTICE("text -> NLG eof: %d, content: %s, time: %u",
              eof, content, (unsigned)text.timeindex);

    if (!s_nlg_in_stream) {
        if (strlen(content) > 0) {
            ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_START, &text);
            __parse_emotion_fields(root, text.timeindex);
            if (eof) {
                AI_NOTIFY_TEXT_T empty = {.data = NULL, .datalen = 0, .timeindex = 0};
                ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_STOP, &empty);
            } else {
                s_nlg_in_stream = true;
            }
        } else if (eof) {
            __parse_emotion_fields(root, text.timeindex);
            ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_STOP, &text);
        } else {
            __parse_emotion_fields(root, text.timeindex);
        }
    } else {
        if (eof) {
            __parse_emotion_fields(root, text.timeindex);
            ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_STOP, &text);
            s_nlg_in_stream = false;
        } else {
            __parse_emotion_fields(root, text.timeindex);
            ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_DATA, &text);
        }
    }

    return OPRT_OK;
}

/**
 * @brief Process AI text data based on type.
 * @param[in] type Text type
 * @param[in] root JSON root
 * @param[in] eof End flag
 * @return OPRT_OK on success
 */
OPERATE_RET ai_text_process(AI_TEXT_TYPE_E type, cJSON *root, BOOL_T eof)
{
    TUYA_CHECK_NULL_RETURN(root, OPRT_INVALID_PARM);

    switch (type) {
    case AI_TEXT_ASR:
        __ai_asr_process(root, eof);
        break;
    case AI_TEXT_NLG:
        __ai_nlg_process(root, eof);
        break;
    case AI_TEXT_SKILL:
        __ai_skills_process(root, eof);
        break;
    case AI_TEXT_CLOUD_EVENT:
        ai_parse_cloud_event(root);
        break;
    default:
        break;
    }

    return OPRT_OK;
}
