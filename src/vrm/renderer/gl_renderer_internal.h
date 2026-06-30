/**
 * @file gl_renderer_internal.h
 * @brief Internal shared declarations for VRM renderer submodules
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef GL_RENDERER_INTERNAL_H
#define GL_RENDERER_INTERNAL_H

#include "vrm_renderer.h"
#include "vrm_loader.h"
#include "vrm_emotion.h"
#include "vrm_lip_sync.h"
#include "svc_ai_player.h"

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef VRM_WINDOW_WIDTH
#define INIT_WIDTH  VRM_WINDOW_WIDTH
#else
#define INIT_WIDTH  1024
#endif

#ifdef VRM_WINDOW_HEIGHT
#define INIT_HEIGHT VRM_WINDOW_HEIGHT
#else
#define INIT_HEIGHT 768
#endif

#define SPEAKING_FADE_MS 500

/* ---------------------------------------------------------------------------
 * GPU mesh wrapper (gl_renderer_shaders.c)
 * --------------------------------------------------------------------------- */
typedef struct {
    GLuint   vao;
    GLuint   vbo;
    GLuint   ebo;
    GLuint   tex_id;
    GLuint   tex_shade;
    GLuint   tex_bump;
    GLuint   tex_emission;
    GLuint   tex_rim;
    GLuint   tex_matcap;
    GLuint   tex_outline_width;
    uint32_t index_count;
    uint32_t vertex_count;
    float    color[4];
    int      has_texture;
    int      has_shade_tex;
    int      has_bump_tex;
    int      has_emission_tex;
    int      has_rim_tex;
    int      has_matcap_tex;
    int      has_outline_width_tex;
    int      has_bones;
    int      has_morph;
    int      use_mtoon;
    int      material_index;
    int      render_queue;
} gpu_mesh_t;

/* ---------------------------------------------------------------------------
 * Shader sources (gl_renderer_shaders.c)
 * --------------------------------------------------------------------------- */
extern const char *s_model_vs;
extern const char *s_model_fs;
extern const char *s_grid_vs;
extern const char *s_grid_fs;
extern const char *s_bg_vs;
extern const char *s_bg_fs;
extern const char *s_shadow_vs;
extern const char *s_shadow_fs;
extern const char *s_ground_shadow_vs;
extern const char *s_ground_shadow_fs;

GLuint __link_program(const char *vs_src, const char *fs_src);
void __upload_mesh(gpu_mesh_t *gpu, const vrm_mesh_t *mesh,
                   const vrm_model_t *model, uint32_t mesh_index);

/* ---------------------------------------------------------------------------
 * Scene helpers (gl_renderer_scene.c)
 * --------------------------------------------------------------------------- */
#define SHADOW_MAP_SIZE 2048

extern GLuint s_grid_vao;
extern GLuint s_grid_vbo;
extern int    s_grid_vert_count;
extern GLuint s_bg_vao;
extern GLuint s_bg_vbo;
extern GLuint s_shadow_fbo;
extern GLuint s_shadow_depth_tex;
extern GLuint s_ground_vao;
extern GLuint s_ground_vbo;
extern GLuint s_ground_ebo;

void __create_grid(void);
void __create_bg_quad(void);
void __create_shadow_fbo(void);
void __create_ground_quad(void);

/* ---------------------------------------------------------------------------
 * Thread-safe API shared state (gl_renderer_api.c)
 * --------------------------------------------------------------------------- */
#define ANIM_NAME_MAX   64
#define EMO_NAME_MAX    64
#define BS_REQ_MAX      16
#define RELOAD_PATH_MAX 1024

typedef struct {
    char  name[64];
    float weight;
} bs_req_entry_t;

typedef struct {
    const char *name;
    int         weight;
} idle_anim_entry_t;

extern emotion_ctx_t *s_emo_ctx;
extern lip_sync_ctx_t s_lip_sync_ctx;
extern AI_PLAYER_CONSUMER_T s_lip_sync_consumer;

extern char             s_anim_req_name[ANIM_NAME_MAX];
extern volatile int     s_anim_req_pending;
extern volatile int     s_anim_req_return_to_idle;
extern volatile Uint32  s_last_interact_ticks;

extern char            s_emo_req_name[EMO_NAME_MAX];
extern float           s_emo_req_intensity;
extern float           s_emo_req_speed;
extern volatile int    s_emo_req_pending;

extern bs_req_entry_t  s_bs_req_entries[BS_REQ_MAX];
extern int             s_bs_req_count;
extern volatile int    s_bs_req_pending;
extern volatile int    s_bs_clear_pending;

extern char         s_reload_model_path[RELOAD_PATH_MAX];
extern volatile int s_reload_model_pending;
extern char         s_reload_scene_dir[RELOAD_PATH_MAX];
extern volatile int s_reload_scene_pending;

extern volatile Uint32 s_speaking_stop_ticks;
extern volatile int    s_speaking_stop_pending;
extern volatile int    s_speaking_active;
extern volatile int    s_subtitle_clear_pending;

extern const idle_anim_entry_t s_idle_pool[];
extern const char             *s_oneshot_anims[];
extern const emotion_id_t      s_idle_emotions[];

#define IDLE_POOL_COUNT    6
#define IDLE_EMOTION_COUNT 14

#define IDLE_SWITCH_MIN_SEC    8.0f
#define IDLE_SWITCH_MAX_SEC   18.0f
#define IDLE_EMOTION_MIN_SEC   8.0f
#define IDLE_EMOTION_MAX_SEC  16.0f
#define IDLE_EMOTION_HOLD_SEC  3.0f
#define IDLE_INTERACT_COOLDOWN 5.0f
#define ANIM_SWITCH_BLEND_SEC  0.60f

/* ---------------------------------------------------------------------------
 * Idle / settings callbacks (gl_renderer_idle.c)
 * --------------------------------------------------------------------------- */
extern volatile int s_sui_anim_req_idx;
extern volatile int s_sui_anim_req_pending;

extern float s_spec_yaw;
extern float s_spec_pitch;
extern float s_spec_dist;
extern float s_spec_target[3];
extern int   s_spec_inited;

int __cmp_str(const void *a, const void *b);
int __is_oneshot(const char *name);
int __find_anim_idx(char **anim_names, int anim_name_count, const char *name);
Uint32 __rand_interval_ms(float min_sec, float max_sec);
float __detect_model_facing(const vrm_model_t *model);
int __pick_idle_anim(char **anim_names, int anim_name_count, int last_pick);
int __pick_recovery_idle_anim(char **anim_names, int anim_name_count, int last_pick);

void __on_model_change(const char *path, void *user_data);
void __on_anim_change(int index, void *user_data);
void __on_scene_change(const char *dir, void *user_data);
void __on_camera_lock(int locked, void *user_data);
void __on_subtitle_toggle(int enabled, void *user_data);
void __on_fullscreen_toggle(int enabled, void *user_data);
void __on_spectator_toggle(int enabled, void *user_data);
void __path_dirname(char *out, size_t out_size, const char *path);
void __path_basename(char *out, size_t out_size, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* GL_RENDERER_INTERNAL_H */
