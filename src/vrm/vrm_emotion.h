/**
 * @file vrm_emotion.h
 * @brief Emotion system for VRM models — predefined expression presets with
 *        smooth transitions, procedural blink, and lip-sync placeholders.
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#ifndef VRM_EMOTION_H
#define VRM_EMOTION_H

#include "vrm_loader.h"
#include "vrm_lip_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EMOTION_NEUTRAL = 0,
    EMOTION_HAPPY,
    EMOTION_SAD,
    EMOTION_ANGRY,
    EMOTION_SURPRISED,
    EMOTION_WINK,
    EMOTION_THINKING,
    EMOTION_COOL,
    EMOTION_RELAXED,
    EMOTION_EMBARRASSED,
    EMOTION_CONFIDENT,
    EMOTION_SLEEP,
    EMOTION_SILLY,
    EMOTION_CONFUSED,
    EMOTION_LOVING,
    EMOTION_LAUGHING,
    EMOTION_SHOCKED,
    EMOTION_FEARFUL,
    EMOTION_KISSY,
    EMOTION_DELICIOUS,
    EMOTION_LIP_AA,
    EMOTION_LIP_IH,
    EMOTION_LIP_OU,
    EMOTION_LIP_EE,
    EMOTION_LIP_OH,
    EMOTION__COUNT
} emotion_id_t;

#define EMOTION_MAX_BINDS     8
#define EMOTION_MAX_OVERRIDES 16

typedef struct {
    char   expr_name[64];
    int    expr_index;
    float  weight;
} emotion_bind_t;

typedef struct {
    const char    *display_name;
    emotion_bind_t binds[EMOTION_MAX_BINDS];
    int            bind_count;
    float          transition_speed;
} emotion_preset_t;

typedef struct {
    vrm_model_t    *model;
    emotion_id_t    current_emotion;
    emotion_id_t    previous_emotion;
    float           blend_t;
    float           transition_speed;
    float           intensity;
    float           smooth_weights[VRM_MAX_EXPRESSIONS];
    float           override_weights[VRM_MAX_EXPRESSIONS];
    uint8_t         override_active[VRM_MAX_EXPRESSIONS];
    int             has_overrides;
    int             auto_blink;
    float           blink_timer;
    float           blink_interval;
    float           blink_weight;
    int             blink_expr_index;
    int             blink_double;
    float           blink_double_gap;
    int             speaking;
    float           speak_timer;
    float           speak_phase;
    int             lip_expr_aa;
    int             lip_expr_ih;
    int             lip_expr_ou;
    int             lip_expr_ee;
    int             lip_expr_oh;
    int             lip_expr_jaw;
    lip_sync_ctx_t *lip_sync;
    float           total_time;
} emotion_ctx_t;

void emotion_init(emotion_ctx_t *ctx, vrm_model_t *model);
void emotion_set(emotion_ctx_t *ctx, emotion_id_t id);
void emotion_set_ex(emotion_ctx_t *ctx, emotion_id_t id, float speed);
void emotion_update(emotion_ctx_t *ctx, float dt);
void emotion_set_speaking(emotion_ctx_t *ctx, int speaking);
void emotion_set_lip_sync(emotion_ctx_t *ctx, lip_sync_ctx_t *ls);
void emotion_set_intensity(emotion_ctx_t *ctx, float intensity);
int emotion_find_by_name(const char *name);
void emotion_set_override(emotion_ctx_t *ctx, int expr_index, float weight);
void emotion_clear_overrides(emotion_ctx_t *ctx);
const char *emotion_name(emotion_id_t id);
const emotion_preset_t *emotion_get_preset(emotion_id_t id);
void emotion_shutdown(emotion_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VRM_EMOTION_H */
