/**
 * @file gl_renderer_api.c
 * @brief VRM renderer submodule (split from gl_renderer.c)
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_renderer.h"
#include "gl_renderer_internal.h"
#include "vrm_loader.h"
#include "mat4_util.h"
#include "vrm_emotion.h"
#include "vrm_spring_bone.h"
#include "vrm_lip_sync.h"
#include "vrm_skybox.h"
#include "vrm_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>

#include "tuya_kconfig.h"
#include "svc_ai_player.h"

#if defined(ENABLE_AUDIO_CODECS) && (ENABLE_AUDIO_CODECS == 1)
#include "tdl_audio_manage.h"
#endif

/* ================================================================== */
/*  Global state — shared with other modules via vrm_viewer_set_speaking */
/* ================================================================== */

/** Emotion context pointer — valid only while vrm_viewer_run() is executing. */
emotion_ctx_t *s_emo_ctx = NULL;

/** Lip sync context — persistent, initialised in vrm_viewer_run(). */
lip_sync_ctx_t s_lip_sync_ctx;

/* ------------------------------------------------------------------ */
/*  Animation switch request (written by any thread, read by render)  */
/* ------------------------------------------------------------------ */

#define ANIM_NAME_MAX 64

/** Pending animation name.  Empty string = no pending request. */
char             s_anim_req_name[ANIM_NAME_MAX];
/** Set to 1 by vrm_viewer_play_animation(), cleared by the render loop. */
volatile int     s_anim_req_pending = 0;
/** External animation requests should play once, then return to idle_normal. */
volatile int     s_anim_req_return_to_idle = 0;

/** Last SDL_GetTicks() when user interaction (MCP request / TTS) occurred.
 *  Written by any thread, read by the render loop. */
volatile Uint32  s_last_interact_ticks = 0;

/* ------------------------------------------------------------------ */
/*  Emotion request (written by any thread, read by render)           */
/* ------------------------------------------------------------------ */

#define EMO_NAME_MAX 64

char            s_emo_req_name[EMO_NAME_MAX];
float           s_emo_req_intensity = 1.0f;
float           s_emo_req_speed     = 0.0f;
volatile int    s_emo_req_pending   = 0;

/* ------------------------------------------------------------------ */
/*  BlendShape override request (written by any thread, read by render) */
/* ------------------------------------------------------------------ */

#define BS_REQ_MAX 16

bs_req_entry_t  s_bs_req_entries[BS_REQ_MAX];
int             s_bs_req_count   = 0;
volatile int    s_bs_req_pending = 0;
volatile int    s_bs_clear_pending = 0;

/* ------------------------------------------------------------------ */
/*  Model / scene reload request (written by settings UI)             */
/* ------------------------------------------------------------------ */

#define RELOAD_PATH_MAX 1024

char         s_reload_model_path[RELOAD_PATH_MAX];
volatile int s_reload_model_pending = 0;

char         s_reload_scene_dir[RELOAD_PATH_MAX];
volatile int s_reload_scene_pending = 0;

/* (Subtitle display is now handled by the LVGL settings panel) */

/* ================================================================== */
/*  Chained audio consumer — forwards PCM to speaker + lip sync      */
/* ================================================================== */

/* Forward-declare the default speaker consumer defined in consumer_speaker.c */
extern AI_PLAYER_CONSUMER_T g_consumer_speaker;

/** Handle obtained from the underlying speaker consumer's open() call. */
static PLAYER_CONSUMER_HANDLE s_speaker_handle = NULL;

static OPERATE_RET __lc_open(PLAYER_CONSUMER_HANDLE *handle)
{
    OPERATE_RET rt = g_consumer_speaker.open(&s_speaker_handle);
    *handle = (PLAYER_CONSUMER_HANDLE)&s_lip_sync_ctx;
    return rt;
}

static OPERATE_RET __lc_close(PLAYER_CONSUMER_HANDLE handle)
{
    (void)handle;
    vrm_viewer_set_speaking(0);
    return g_consumer_speaker.close(s_speaker_handle);
}

static OPERATE_RET __lc_start(PLAYER_CONSUMER_HANDLE handle)
{
    (void)handle;
    vrm_viewer_set_speaking(1);
    return g_consumer_speaker.start(s_speaker_handle);
}

static OPERATE_RET __lc_write(PLAYER_CONSUMER_HANDLE handle,
                               const void *buf, uint32_t len)
{
    (void)handle;

    lip_sync_feed_pcm(&s_lip_sync_ctx, buf, len, 1, 16);

    OPERATE_RET rt = g_consumer_speaker.write(s_speaker_handle, buf, len);

#if defined(ENABLE_AUDIO_CODECS) && (ENABLE_AUDIO_CODECS == 1)
    if (rt == OPRT_OK && s_speaker_handle) {
        uint32_t delay_frames = 0;
        if (tdl_audio_get_playback_delay_frames(
                (TDL_AUDIO_HANDLE_T)s_speaker_handle, &delay_frames) == OPRT_OK) {
            lip_sync_set_playback_delay_frames(&s_lip_sync_ctx, delay_frames);
        }
    }
#endif

    return rt;
}

static OPERATE_RET __lc_stop(PLAYER_CONSUMER_HANDLE handle)
{
    (void)handle;
    vrm_viewer_set_speaking(0);
    return g_consumer_speaker.stop(s_speaker_handle);
}

static OPERATE_RET __lc_set_volume(PLAYER_CONSUMER_HANDLE handle,
                                    uint32_t volume)
{
    (void)handle;
    return g_consumer_speaker.set_volume(s_speaker_handle, volume);
}

AI_PLAYER_CONSUMER_T s_lip_sync_consumer = {
    .open       = __lc_open,
    .close      = __lc_close,
    .start      = __lc_start,
    .write      = __lc_write,
    .stop       = __lc_stop,
    .set_volume = __lc_set_volume,
};

/* ================================================================== */
/*  Idle behavior — configuration                                      */
/* ================================================================== */

const idle_anim_entry_t s_idle_pool[] = {
    { "idle_normal",        10 },
    { "idle_boring",         3 },
    { "happy_idle",          3 },
    { "thinking",            2 },
    { "squat",               1 },
    { "show",                1 },
};

const char *s_oneshot_anims[] = {
    "say_hello", "standing_greeting", "wave",
    "excited", "joy", "crying",
    "show", "squat", "shoot", "bier",
    "look_around", "thinking",
    NULL
};

const emotion_id_t s_idle_emotions[] = {
    EMOTION_NEUTRAL, EMOTION_NEUTRAL, EMOTION_NEUTRAL,
    EMOTION_HAPPY, EMOTION_HAPPY,
    EMOTION_WINK,
    EMOTION_THINKING, EMOTION_THINKING,
    EMOTION_COOL,
    EMOTION_RELAXED, EMOTION_RELAXED,
    EMOTION_SILLY,
    EMOTION_CONFUSED,
    EMOTION_LOVING,
};

/* ================================================================== */
/*  Public: speaking control (called from any thread)                 */
/* ================================================================== */

volatile Uint32 s_speaking_stop_ticks   = 0;
volatile int    s_speaking_stop_pending  = 0;
volatile int    s_speaking_active        = 0;
volatile int    s_subtitle_clear_pending = 0;

void vrm_viewer_set_speaking(int speaking)
{
    s_last_interact_ticks = SDL_GetTicks();

    /*
     * Audio player callbacks run on the ai_player thread; emotion_ctx is
     * owned by the SDL render thread.  Only flip volatile flags here and
     * let the render loop call emotion_set_speaking() on the correct thread.
     */
    if (speaking) {
        s_speaking_stop_pending    = 0;
        s_subtitle_clear_pending   = 0;
        s_speaking_active          = 1;
    } else {
        s_speaking_stop_ticks      = SDL_GetTicks();
        s_speaking_stop_pending    = 1;
        s_subtitle_clear_pending   = 1;
    }
}

void vrm_viewer_play_animation(const char *name)
{
    if (!name || name[0] == '\0') return;
    s_last_interact_ticks = SDL_GetTicks();
    strncpy(s_anim_req_name, name, ANIM_NAME_MAX - 1);
    s_anim_req_name[ANIM_NAME_MAX - 1] = '\0';
    s_anim_req_return_to_idle = 1;
    s_anim_req_pending = 1;
}

void vrm_viewer_set_emotion(const char *name, float intensity, float speed)
{
    if (!name || name[0] == '\0') return;
    s_last_interact_ticks = SDL_GetTicks();
    strncpy(s_emo_req_name, name, EMO_NAME_MAX - 1);
    s_emo_req_name[EMO_NAME_MAX - 1] = '\0';
    s_emo_req_intensity = intensity;
    s_emo_req_speed     = speed;
    s_emo_req_pending   = 1;
}

void vrm_viewer_set_blendshape(const char *expr_name, float weight)
{
    if (!expr_name || expr_name[0] == '\0') return;
    s_last_interact_ticks = SDL_GetTicks();
    int idx = s_bs_req_count;
    if (idx >= BS_REQ_MAX) return;
    strncpy(s_bs_req_entries[idx].name, expr_name, 63);
    s_bs_req_entries[idx].name[63] = '\0';
    s_bs_req_entries[idx].weight   = weight;
    s_bs_req_count = idx + 1;
    s_bs_req_pending = 1;
}

void vrm_viewer_clear_blendshapes(void)
{
    s_last_interact_ticks = SDL_GetTicks();
    s_bs_clear_pending = 1;
}

void vrm_viewer_set_subtitle(const char *text)
{
    s_subtitle_clear_pending = 0;
    vrm_overlay_set_subtitle(text);
}

void vrm_viewer_reload_model(const char *model_path)
{
    if (!model_path || model_path[0] == '\0') {
        return;
    }
    strncpy(s_reload_model_path, model_path, RELOAD_PATH_MAX - 1);
    s_reload_model_path[RELOAD_PATH_MAX - 1] = '\0';
    s_reload_model_pending = 1;
}

void vrm_viewer_reload_scene(const char *scene_dir)
{
    if (!scene_dir) {
        s_reload_scene_dir[0] = '\0';
    } else {
        strncpy(s_reload_scene_dir, scene_dir, RELOAD_PATH_MAX - 1);
        s_reload_scene_dir[RELOAD_PATH_MAX - 1] = '\0';
    }
    s_reload_scene_pending = 1;
}

