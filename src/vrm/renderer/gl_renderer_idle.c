/**
 * @file gl_renderer_idle.c
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

int __cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/* ================================================================== */
/*  Idle behavior — helpers                                            */
/* ================================================================== */

/**
 * @brief Check if an animation name is one-shot (play once then return to idle)
 * @param[in] name animation name
 * @return 1 if one-shot, 0 if looping
 */
int __is_oneshot(const char *name)
{
    for (int i = 0; s_oneshot_anims[i]; i++) {
        if (strcmp(name, s_oneshot_anims[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int __find_anim_idx(char **anim_names, int anim_name_count, const char *name)
{
    if (!anim_names || anim_name_count <= 0 || !name || name[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < anim_name_count; i++) {
        if (strcmp(anim_names[i], name) == 0) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief Generate a random float in [lo, hi]
 * @param[in] lo lower bound
 * @param[in] hi upper bound
 * @return random value
 */
float __rand_range_f(float lo, float hi)
{
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

/**
 * @brief Generate a random interval in milliseconds from a [min, max] seconds range
 * @param[in] min_sec minimum seconds
 * @param[in] max_sec maximum seconds
 * @return interval in milliseconds
 */
Uint32 __rand_interval_ms(float min_sec, float max_sec)
{
    return (Uint32)(__rand_range_f(min_sec, max_sec) * 1000.0f);
}

/**
 * @brief Detect Y-rotation needed so the model faces the camera (+Z).
 *
 * Uses the humanoid bone map to find "leftUpperArm" and "rightUpperArm"
 * rest-pose world positions. In a correctly oriented model (facing +Z
 * toward the camera at +Z), leftUpperArm.x should be greater than
 * rightUpperArm.x in world space.  If reversed, the model faces -Z
 * and needs a 180-degree Y rotation.
 *
 * @param[in] model loaded VRM model with humanoid_map populated
 * @return rotation angle in radians (0 or M_PI)
 */
float __detect_model_facing(const vrm_model_t *model)
{
    if (!model->humanoid_map || model->humanoid_map_count == 0 ||
        !model->bones || model->bone_count == 0) {
        return 0.0f;
    }

    int left_bone  = -1;
    int right_bone = -1;

    for (uint32_t i = 0; i < model->humanoid_map_count; i++) {
        const char *hname = model->humanoid_map[i].humanoid_name;
        const char *nname = model->humanoid_map[i].node_name;
        if (!nname[0]) continue;

        int bone_idx = -1;
        for (uint32_t b = 0; b < model->bone_count; b++) {
            if (strcmp(model->bones[b].name, nname) == 0) {
                bone_idx = (int)b;
                break;
            }
        }
        if (bone_idx < 0) continue;

        if (strcmp(hname, "leftUpperArm") == 0)  left_bone  = bone_idx;
        if (strcmp(hname, "rightUpperArm") == 0) right_bone = bone_idx;
    }

    if (left_bone < 0 || right_bone < 0) {
        printf("[vrm] facing detect: missing arm bones, assuming front-facing\n");
        return 0.0f;
    }

    /* Compute rest-pose global transforms */
    uint32_t nb = model->bone_count;
    float *globals = (float *)malloc(nb * 16 * sizeof(float));
    if (!globals) return 0.0f;

    for (uint32_t i = 0; i < nb; i++) {
        if (model->bones[i].parent < 0) {
            memcpy(&globals[i * 16], model->bones[i].local_transform, 16 * sizeof(float));
        } else {
            int p = model->bones[i].parent;
            mat4_multiply(&globals[i * 16], &globals[p * 16],
                          model->bones[i].local_transform);
        }
    }

    /* World X of each arm = column 3 (translation), element [12] of the 4x4 */
    float lx = globals[left_bone  * 16 + 12];
    float rx = globals[right_bone * 16 + 12];

    free(globals);

    /*
     * Camera is at +Z looking in -Z.
     * Model facing +Z (toward camera): leftArm.x > rightArm.x  → OK
     * Model facing -Z (away):          leftArm.x < rightArm.x  → rotate 180°
     */
    if (lx < rx) {
        printf("[vrm] facing detect: model faces -Z (lx=%.3f rx=%.3f), rotating 180°\n",
               lx, rx);
        return (float)M_PI;
    }

    printf("[vrm] facing detect: model faces +Z (lx=%.3f rx=%.3f), no rotation\n",
           lx, rx);
    return 0.0f;
}

/**
 * @brief Weighted random selection of an idle animation from loaded pool
 * @param[in] anim_names loaded animation names array
 * @param[in] anim_name_count number of loaded animations
 * @param[in] last_pick index to skip for variety (-1 = no skip)
 * @return index into anim_names, or -1 if none available
 */
int __pick_idle_anim(char **anim_names, int anim_name_count, int last_pick)
{
    int avail[IDLE_POOL_COUNT];
    int avail_weights[IDLE_POOL_COUNT];
    int avail_count  = 0;
    int total_weight = 0;

    for (int p = 0; p < IDLE_POOL_COUNT; p++) {
        for (int a = 0; a < anim_name_count; a++) {
            if (strcmp(anim_names[a], s_idle_pool[p].name) == 0) {
                if (a == last_pick) {
                    break;
                }
                avail[avail_count]        = a;
                avail_weights[avail_count] = s_idle_pool[p].weight;
                total_weight += s_idle_pool[p].weight;
                avail_count++;
                break;
            }
        }
    }

    /* Fallback: allow last_pick if it was the only available entry */
    if (avail_count == 0) {
        for (int p = 0; p < IDLE_POOL_COUNT; p++) {
            for (int a = 0; a < anim_name_count; a++) {
                if (strcmp(anim_names[a], s_idle_pool[p].name) == 0) {
                    avail[avail_count]        = a;
                    avail_weights[avail_count] = s_idle_pool[p].weight;
                    total_weight += s_idle_pool[p].weight;
                    avail_count++;
                    break;
                }
            }
        }
    }

    if (avail_count == 0 || total_weight == 0) {
        return -1;
    }

    int roll = rand() % total_weight;
    int sum  = 0;
    for (int i = 0; i < avail_count; i++) {
        sum += avail_weights[i];
        if (roll < sum) {
            return avail[i];
        }
    }
    return avail[avail_count - 1];
}

int __pick_recovery_idle_anim(char **anim_names, int anim_name_count, int last_pick)
{
    int idx = __find_anim_idx(anim_names, anim_name_count, "idle_normal");
    if (idx >= 0) {
        return idx;
    }

    return __pick_idle_anim(anim_names, anim_name_count, last_pick);
}

/* ================================================================== */
/*  LVGL settings UI callbacks                                         */
/* ================================================================== */

/**
 * @brief LVGL settings callback: user selected a new model.
 * @param[in] path      Full path to the model file
 * @param[in] user_data Unused
 * @return none
 */
void __on_model_change(const char *path, void *user_data)
{
    (void)user_data;
    vrm_viewer_reload_model(path);
}

/**
 * @brief LVGL settings callback: user selected an animation by index.
 *
 * Uses the same pending-flag mechanism as vrm_viewer_play_animation()
 * to pass the request safely to the render loop.
 */
volatile int s_sui_anim_req_idx     = -1;
volatile int s_sui_anim_req_pending = 0;

void __on_anim_change(int index, void *user_data)
{
    (void)user_data;
    s_sui_anim_req_idx     = index;
    s_sui_anim_req_pending = 1;
}

/**
 * @brief Settings callback: user selected a scene sub-folder or empty scene.
 * @param[in] dir       Scene sub-directory path ("" for no scene)
 * @param[in] user_data Unused
 * @return none
 */
void __on_scene_change(const char *dir, void *user_data)
{
    (void)user_data;
    vrm_viewer_reload_scene(dir);
}

/**
 * @brief LVGL settings callback: camera lock toggled.
 * @param[in] locked    1 = locked, 0 = unlocked
 * @param[in] user_data Unused
 * @return none
 */
void __on_camera_lock(int locked, void *user_data)
{
    (void)user_data;
    (void)locked;
}

/**
 * @brief LVGL settings callback: subtitle toggled.
 * @param[in] enabled   1 = enabled, 0 = disabled
 * @param[in] user_data Unused
 * @return none
 */
void __on_subtitle_toggle(int enabled, void *user_data)
{
    (void)user_data;
    (void)enabled;
}

/**
 * @brief LVGL settings callback: fullscreen toggled.
 * @param[in] enabled   1 = fullscreen, 0 = windowed
 * @param[in] user_data Pointer to SDL_Window
 * @return none
 */
void __on_fullscreen_toggle(int enabled, void *user_data)
{
    SDL_Window *win = (SDL_Window *)user_data;
    if (!win) {
        return;
    }
    SDL_SetWindowFullscreen(win,
        enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

float s_spec_yaw      = 0.0f;
float s_spec_pitch    = 0.15f;
float s_spec_dist     = 3.0f;
float s_spec_target[3] = {0};
int   s_spec_inited   = 0;

/**
 * @brief Settings callback: spectator mode toggled.
 * @param[in] enabled   1 = spectator on, 0 = off
 * @param[in] user_data unused
 * @return none
 */
void __on_spectator_toggle(int enabled, void *user_data)
{
    (void)user_data;
    if (enabled) {
        s_spec_inited = 0;
    }
}

/**
 * @brief Extract the directory portion of a file path.
 * @param[out] out      Output buffer
 * @param[in]  out_size Buffer size
 * @param[in]  path     File path
 * @return none
 */
void __path_dirname(char *out, size_t out_size, const char *path)
{
    char tmp[RELOAD_PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *d = dirname(tmp);
    strncpy(out, d, out_size - 1);
    out[out_size - 1] = '\0';
}

/**
 * @brief Extract the filename portion of a file path.
 * @param[out] out      Output buffer
 * @param[in]  out_size Buffer size
 * @param[in]  path     File path
 * @return none
 */
void __path_basename(char *out, size_t out_size, const char *path)
{
    char tmp[RELOAD_PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *b = basename(tmp);
    strncpy(out, b, out_size - 1);
    out[out_size - 1] = '\0';
}
