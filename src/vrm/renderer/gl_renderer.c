/**
 * @file gl_renderer.c
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
#include "vrm_text_timeline.h"
#include "vrm_skybox.h"
#include "vrm_bg2d.h"
#include "vrm_overlay.h"
#include "mtoon_shaders.h"
#include "mtoon_outline.h"
#include "mtoon_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>

#include "svc_ai_player.h"

/**
 * @brief Load cubemap skybox or 2D background when user selects a scene folder
 * @param[out] skybox  Skybox context
 * @param[out] bg2d    2D background context
 * @param[in]  dir     Scene sub-directory path ("" = empty scene / gradient)
 * @return none
 */
static void __load_scene_background(skybox_t *skybox, bg2d_t *bg2d, const char *dir)
{
    skybox_destroy(skybox);
    bg2d_destroy(bg2d);
    memset(skybox, 0, sizeof(*skybox));
    memset(bg2d, 0, sizeof(*bg2d));

    if (!dir || dir[0] == '\0') {
        return;
    }

    printf("[vrm_viewer] loading scene from: %s\n", dir);
    if (skybox_init(skybox, dir) == 0) {
        return;
    }
    if (bg2d_init(bg2d, dir) == 0) {
        return;
    }
    printf("[vrm_viewer] scene not available — using gradient background\n");
}

int vrm_viewer_run(const char *model_path, const char *vrma_dir)
{
    /* ---- load model ---- */
    vrm_model_t model;
    printf("[vrm_viewer] loading model: %s\n", model_path);
    if (vrm_model_load(&model, model_path) != 0) {
        fprintf(stderr, "[vrm_viewer] failed to load model\n");
        return -1;
    }

    /* ---- scan vrma directory and load all .vrma files ---- */
    uint32_t active_anim = 0;
    int has_anim = 0;

    /* Store animation names for display */
    #define MAX_VRMA_FILES 64
    char *anim_names[MAX_VRMA_FILES];
    int anim_name_count = 0;
    uint32_t first_vrma_anim = 0; /* index of the first VRMA animation in model */

    if (vrma_dir && vrma_dir[0] != '\0') {
        /* Collect .vrma filenames */
        char *vrma_files[MAX_VRMA_FILES];
        int vrma_count = 0;

        DIR *dir = opendir(vrma_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL && vrma_count < MAX_VRMA_FILES) {
                const char *name = entry->d_name;
                size_t len = strlen(name);
                if (len > 5 && strcasecmp(name + len - 5, ".vrma") == 0) {
                    vrma_files[vrma_count] = strdup(name);
                    vrma_count++;
                }
            }
            closedir(dir);
        }

        /* Sort alphabetically */
        if (vrma_count > 1)
            qsort(vrma_files, vrma_count, sizeof(char *), __cmp_str);

        printf("[vrm_viewer] found %d VRMA files in %s\n", vrma_count, vrma_dir);
        first_vrma_anim = model.animation_count;

        /* Load each VRMA */
        for (int i = 0; i < vrma_count; i++) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", vrma_dir, vrma_files[i]);
            printf("[vrm_viewer]   [%d] %s ... ", i, vrma_files[i]);
            if (vrm_load_vrma(&model, full_path) == 0) {
                printf("OK (duration=%.1fs)\n", model.animations[model.animation_count - 1].duration);
                /* Store display name (strip .vrma extension) */
                char *dname = strdup(vrma_files[i]);
                size_t dlen = strlen(dname);
                if (dlen > 5) dname[dlen - 5] = '\0';
                anim_names[anim_name_count++] = dname;
            } else {
                printf("FAILED\n");
            }
            free(vrma_files[i]);
        }

        if (anim_name_count > 0) {
            int idle_idx = __find_anim_idx(anim_names, anim_name_count, "idle_normal");
            active_anim = (idle_idx >= 0)
                ? (first_vrma_anim + (uint32_t)idle_idx)
                : first_vrma_anim;
            has_anim = 1;
        }
    }

    /* If model has embedded animations and no VRMA was loaded, use first embedded */
    if (!has_anim && model.animation_count > 0) {
        active_anim = 0;
        has_anim = 1;
    }

    uint32_t total_anims = model.animation_count;

    /* ---- SDL2 + OpenGL init ---- */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[vrm_viewer] SDL_Init failed: %s\n", SDL_GetError());
        vrm_model_free(&model);
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#ifdef VRM_FULLSCREEN
    win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif

    /* Try GL contexts in order: 3.3 Core → 3.1 + MSAA → 3.1 without MSAA */
    typedef struct { int major, minor, profile, msaa; } gl_try_t;
    gl_try_t gl_tries[] = {
        { 3, 3, SDL_GL_CONTEXT_PROFILE_CORE, 4 },
        { 3, 1, 0, 4 },
        { 3, 1, 0, 0 },
    };

    SDL_Window *window = NULL;
    SDL_GLContext gl_ctx = NULL;
    for (int ti = 0; ti < 3; ti++) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_tries[ti].major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_tries[ti].minor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, gl_tries[ti].profile);
        if (gl_tries[ti].msaa > 0) {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, gl_tries[ti].msaa);
        } else {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        }
        window = SDL_CreateWindow(
            "VRM Viewer — TuyaOpen",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            INIT_WIDTH, INIT_HEIGHT, win_flags);
        if (!window) continue;
        gl_ctx = SDL_GL_CreateContext(window);
        if (gl_ctx) {
            printf("[vrm_viewer] GL context: %d.%d %s MSAA=%d\n",
                   gl_tries[ti].major, gl_tries[ti].minor,
                   gl_tries[ti].profile ? "core" : "compat",
                   gl_tries[ti].msaa);
            break;
        }
        SDL_DestroyWindow(window);
        window = NULL;
    }
    if (!window || !gl_ctx) {
        fprintf(stderr, "[vrm_viewer] Failed to create GL context: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); vrm_model_free(&model); return -1;
    }

    SDL_GL_SetSwapInterval(1);
    SDL_GL_MakeCurrent(window, gl_ctx);

    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        fprintf(stderr, "[vrm_viewer] glewInit failed: %s\n",
                glewGetErrorString(glew_err));
        SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(window);
        SDL_Quit(); vrm_model_free(&model); return -1;
    }
    /* Core profile may leave a benign GL_INVALID_ENUM after glewInit(). */
    glGetError();

    printf("[vrm_viewer] GL version : %s\n", glGetString(GL_VERSION));
    printf("[vrm_viewer] GL renderer: %s\n", glGetString(GL_RENDERER));

    /* ---- compile shaders ---- */
    GLuint model_prog = __link_program(s_model_vs, s_model_fs);
    GLuint mtoon_prog = mtoon_create_program();
    GLuint outline_prog = mtoon_outline_create_program();
    GLuint grid_prog  = __link_program(s_grid_vs, s_grid_fs);
    GLuint bg_prog    = __link_program(s_bg_vs, s_bg_fs);
    GLuint shadow_prog = __link_program(s_shadow_vs, s_shadow_fs);
    GLuint ground_shadow_prog = __link_program(s_ground_shadow_vs,
                                               s_ground_shadow_fs);

    /* ---- upload meshes ---- */
    gpu_mesh_t *gpu = (gpu_mesh_t *)calloc(model.mesh_count, sizeof(gpu_mesh_t));
    for (uint32_t i = 0; i < model.mesh_count; i++) {
        __upload_mesh(&gpu[i], &model.meshes[i], &model, i);
    }

    printf("[vrm_viewer] uploaded %u meshes to GPU\n", model.mesh_count);

    __create_grid();
    __create_bg_quad();
    __create_shadow_fbo();
    __create_ground_quad();

    /* ---- Scene background (empty by default; user picks in settings) ---- */
    skybox_t skybox;
    bg2d_t bg2d;
    memset(&skybox, 0, sizeof(skybox));
    memset(&bg2d, 0, sizeof(bg2d));

    /* ---- Bone matrix TBO ---- */
    GLuint bone_tbo_tex = 0, bone_tbo_buf = 0;
    float *bone_matrices = NULL;
    float *target_bone_matrices = NULL;
    float *blend_from_matrices = NULL;
    uint32_t bone_count = model.bone_count;
    Uint32 anim_blend_start_ticks = 0;
    int anim_blend_active = 0;

    if (bone_count > 0) {
        bone_matrices = (float *)calloc(bone_count * 16, sizeof(float));
        target_bone_matrices = (float *)calloc(bone_count * 16, sizeof(float));
        blend_from_matrices = (float *)calloc(bone_count * 16, sizeof(float));

        /* Initialize to rest pose */
        vrm_rest_pose_matrices(&model, bone_matrices);
        memcpy(target_bone_matrices, bone_matrices, bone_count * 16 * sizeof(float));
        memcpy(blend_from_matrices, bone_matrices, bone_count * 16 * sizeof(float));

        glGenBuffers(1, &bone_tbo_buf);
        glBindBuffer(GL_TEXTURE_BUFFER, bone_tbo_buf);
        glBufferData(GL_TEXTURE_BUFFER,
                     (GLsizeiptr)(bone_count * 16 * sizeof(float)),
                     bone_matrices, GL_DYNAMIC_DRAW);

        glGenTextures(1, &bone_tbo_tex);
        glBindTexture(GL_TEXTURE_BUFFER, bone_tbo_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, bone_tbo_buf);

        glBindTexture(GL_TEXTURE_BUFFER, 0);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);

        printf("[vrm_viewer] bone TBO: %u bones, %zu bytes\n",
               bone_count, (size_t)(bone_count * 16 * sizeof(float)));
    }

    /* ---- auto-detect model facing and compute Y correction ---- */
    float model_y_rot_base = __detect_model_facing(&model);
    float model_y_rot = model_y_rot_base;
    float model_x_rot = 0.0f;

    /* ---- camera state ---- */
    float cam_yaw   = 0.0f;
    float cam_pitch = 0.15f;
    float cam_dist  = model.extent * 2.0f;
    if (cam_dist < 1.0f) cam_dist = 3.0f;
    float cam_target[3] = {
        model.center[0], model.center[1], model.center[2]
    };

    /* ---- GL state ---- */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_CULL_FACE);

    /* White fallback texture */
    GLuint white_tex;
    {
        uint8_t white_pixel[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &white_tex);
        glBindTexture(GL_TEXTURE_2D, white_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    /* ---- Morph target info ---- */
    int total_morph_targets = 0;
    for (uint32_t i = 0; i < model.mesh_count; i++)
        total_morph_targets += (int)model.meshes[i].morph_target_count;
    printf("[vrm_viewer] total morph targets across all meshes: %d\n", total_morph_targets);
    printf("[vrm_viewer] expressions: %u\n", model.expression_count);

    /* ---- Emotion system ---- */
    emotion_ctx_t emo_ctx;
    emotion_init(&emo_ctx, &model);
    emotion_id_t cur_emo = EMOTION_NEUTRAL;
    Uint32 last_frame_ticks = SDL_GetTicks();

    /* ---- Audio-driven lip sync (delay from LIP_SYNC_FALLBACK_DELAY_MS) ---- */
    lip_sync_init(&s_lip_sync_ctx, 16000);
    emotion_set_lip_sync(&emo_ctx, &s_lip_sync_ctx);

    /* Register the chained consumer so decoded PCM is also fed to lip sync.
     * This replaces the default speaker consumer with a wrapper that both
     * outputs audio to the hardware AND analyses energy for lip sync. */
    tuya_ai_player_set_consumer(&s_lip_sync_consumer);

    /* Publish the emotion context so vrm_viewer_set_speaking() can reach it */
    s_emo_ctx = &emo_ctx;

    /* ---- Spring bone physics ---- */
    spring_bone_ctx_t spring_ctx;
    spring_bone_init(&spring_ctx, &model);

    /* ---- Settings overlay (rendered in this GL window) ---- */
    {
        char model_dir[RELOAD_PATH_MAX];
        char model_basename[256];
        char scene_parent[RELOAD_PATH_MAX];

        __path_dirname(model_dir, sizeof(model_dir), model_path);
        __path_basename(model_basename, sizeof(model_basename), model_path);

        scene_parent[0] = '\0';
#ifdef VRM_SCENE_PARENT_DIR
        if (VRM_SCENE_PARENT_DIR[0] != '\0') {
            snprintf(scene_parent, sizeof(scene_parent), "%s", VRM_SCENE_PARENT_DIR);
        }
#endif

        int init_fullscreen = (SDL_GetWindowFlags(window) &
                               SDL_WINDOW_FULLSCREEN_DESKTOP) ? 1 : 0;

        vrm_overlay_cfg_t sui_cfg = {
            .model_dir        = model_dir,
            .current_model    = model_basename,
            .scene_parent_dir = scene_parent,
            .current_scene    = "",
            .anim_names       = anim_names,
            .anim_count       = anim_name_count,
            .active_anim      = 0,
            .camera_locked    = 0,
            .subtitle_enabled = 1,
            .fullscreen       = init_fullscreen,
            .on_model_change  = __on_model_change,
            .on_anim_change   = __on_anim_change,
            .on_scene_change  = __on_scene_change,
            .on_camera_lock   = __on_camera_lock,
            .on_subtitle      = __on_subtitle_toggle,
            .on_fullscreen    = __on_fullscreen_toggle,
            .on_spectator     = __on_spectator_toggle,
            .user_data        = window,
        };

        vrm_overlay_init(&sui_cfg);
    }

    /* ---- main loop ---- */
    bool running   = true;
    bool dragging  = false;
    bool paused    = false;
    int  last_mx   = 0, last_my = 0;
    int  win_w     = INIT_WIDTH, win_h = INIT_HEIGHT;

    Uint32 start_ticks = SDL_GetTicks();

    /* ---- idle behavior state ---- */
    srand((unsigned)SDL_GetTicks());
    s_last_interact_ticks = SDL_GetTicks();
    int     idle_last_pick    = -1;
    int     idle_is_oneshot   = 0;
    int     idle_was_active   = 0;
    Uint32  idle_next_switch  = SDL_GetTicks() + __rand_interval_ms(IDLE_SWITCH_MIN_SEC, IDLE_SWITCH_MAX_SEC);
    Uint32  idle_next_emotion = SDL_GetTicks() + __rand_interval_ms(IDLE_EMOTION_MIN_SEC, IDLE_EMOTION_MAX_SEC);
    Uint32  idle_emotion_revert = 0;

#define START_ANIM_BLEND()                                                     \
    do {                                                                       \
        if (bone_count > 0 && bone_matrices && blend_from_matrices) {          \
            memcpy(blend_from_matrices, bone_matrices,                         \
                   bone_count * 16 * sizeof(float));                           \
            anim_blend_start_ticks = SDL_GetTicks();                           \
            anim_blend_active = 1;                                             \
        }                                                                      \
    } while (0)

    while (running) {
        /* ---- events ---- */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (vrm_overlay_handle_event(&ev, win_w, win_h)) {
                continue;
            }
            int cam_locked = vrm_overlay_camera_locked();
            int spectator  = vrm_overlay_spectator();

            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
                if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                    win_w = ev.window.data1;
                    win_h = ev.window.data2;
                    glViewport(0, 0, win_w, win_h);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT ||
                    ev.button.button == SDL_BUTTON_RIGHT ||
                    ev.button.button == SDL_BUTTON_MIDDLE) {
                    dragging = true;
                    last_mx = ev.button.x; last_my = ev.button.y;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT ||
                    ev.button.button == SDL_BUTTON_RIGHT ||
                    ev.button.button == SDL_BUTTON_MIDDLE) {
                    dragging = false;
                }
                break;
            case SDL_MOUSEMOTION:
                if (dragging) {
                    int dx = ev.motion.x - last_mx;
                    int dy = ev.motion.y - last_my;
                    if (spectator) {
                        Uint32 btns = SDL_GetMouseState(NULL, NULL);
                        int rmb = btns & SDL_BUTTON(SDL_BUTTON_RIGHT);
                        int mmb = btns & SDL_BUTTON(SDL_BUTTON_MIDDLE);
                        if (rmb || mmb) {
                            float pan_scale = s_spec_dist * 0.002f;
                            float rx = cosf(s_spec_yaw);
                            float rz = -sinf(s_spec_yaw);
                            s_spec_target[0] -= rx * (float)dx * pan_scale;
                            s_spec_target[2] -= rz * (float)dx * pan_scale;
                            s_spec_target[1] += (float)dy * pan_scale;
                        } else {
                            s_spec_yaw   += (float)dx * 0.005f;
                            s_spec_pitch -= (float)dy * 0.005f;
                            if (s_spec_pitch >  1.55f) s_spec_pitch =  1.55f;
                            if (s_spec_pitch < -1.55f) s_spec_pitch = -1.55f;
                        }
                    } else if (!cam_locked) {
                        model_y_rot += dx * 0.005f;
                    }
                    last_mx = ev.motion.x; last_my = ev.motion.y;
                }
                break;
            case SDL_MOUSEWHEEL:
                if (spectator) {
                    float zoom = (ev.wheel.y > 0) ? 0.85f : 1.18f;
                    s_spec_dist *= zoom;
                    if (s_spec_dist < 0.1f) s_spec_dist = 0.1f;
                    if (s_spec_dist > 200.0f) s_spec_dist = 200.0f;
                }
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (ev.key.keysym.sym == SDLK_r) {
                    model_y_rot = model_y_rot_base;
                    model_x_rot = 0.0f;
                }
                if (ev.key.keysym.sym == SDLK_SPACE) paused = !paused;
                if (ev.key.keysym.sym == SDLK_LEFT || ev.key.keysym.sym == SDLK_RIGHT) {
                    int anim_total = anim_name_count > 0 ? anim_name_count : (int)total_anims;
                    if (has_anim && anim_total > 0) {
                        int step = (ev.key.keysym.sym == SDLK_RIGHT) ? 1 : -1;
                        int anim_idx = 0;
                        START_ANIM_BLEND();

                        if (anim_name_count > 0) {
                            if (active_anim >= first_vrma_anim) {
                                anim_idx = (int)(active_anim - first_vrma_anim);
                            }
                            anim_idx = (anim_idx + step + anim_name_count) % anim_name_count;
                            active_anim = first_vrma_anim + (uint32_t)anim_idx;
                            idle_last_pick = anim_idx;
                            idle_is_oneshot = __is_oneshot(anim_names[anim_idx]);
                        } else {
                            anim_idx = ((int)active_anim + step + anim_total) % anim_total;
                            active_anim = (uint32_t)anim_idx;
                            idle_is_oneshot = 0;
                        }

                        has_anim = 1;
                        start_ticks = SDL_GetTicks();
                        s_last_interact_ticks = start_ticks;
                        idle_next_switch = start_ticks +
                            __rand_interval_ms(IDLE_SWITCH_MIN_SEC, IDLE_SWITCH_MAX_SEC);
                    }
                }
                /* E key: cycle through emotions */
                if (ev.key.keysym.sym == SDLK_e) {
                    cur_emo = (emotion_id_t)((int)cur_emo + 1);
                    if ((int)cur_emo >= EMOTION__COUNT) cur_emo = EMOTION_NEUTRAL;
                    emotion_set(&emo_ctx, cur_emo);
                }
                /* F1-F6: direct emotion set */
                if (ev.key.keysym.sym >= SDLK_F1 && ev.key.keysym.sym <= SDLK_F6) {
                    emotion_id_t eid = (emotion_id_t)(ev.key.keysym.sym - SDLK_F1);
                    if ((int)eid < EMOTION__COUNT) {
                        cur_emo = eid;
                        emotion_set(&emo_ctx, cur_emo);
                    }
                }
                /* B key: toggle auto-blink */
                if (ev.key.keysym.sym == SDLK_b) {
                    emo_ctx.auto_blink = !emo_ctx.auto_blink;
                }
                /* T key: toggle speaking */
                if (ev.key.keysym.sym == SDLK_t) {
                    emotion_set_speaking(&emo_ctx, !emo_ctx.speaking);
                }
                /* P key: toggle spring bone physics */
                if (ev.key.keysym.sym == SDLK_p) {
                    spring_ctx.enabled = !spring_ctx.enabled;
                    if (!spring_ctx.enabled) spring_ctx.initialized = 0;
                }
                /* Spring bone parameter tuning:
                 *   A / S : decrease / increase stiffness
                 *   D / F : decrease / increase drag
                 *   G / H : decrease / increase gravity power
                 *   J / K : decrease / increase hit radius
                 *   L     : reset parameters to VRM defaults
                 */
                if (ev.key.keysym.sym == SDLK_a || ev.key.keysym.sym == SDLK_s) {
                    float delta = (ev.key.keysym.sym == SDLK_s) ? 0.1f : -0.1f;
                    for (int ci = 0; ci < spring_ctx.chain_count; ci++) {
                        for (int ji = 0; ji < spring_ctx.chains[ci].joint_count; ji++) {
                            spring_ctx.chains[ci].joints[ji].stiffness += delta;
                            if (spring_ctx.chains[ci].joints[ji].stiffness < 0.0f)
                                spring_ctx.chains[ci].joints[ji].stiffness = 0.0f;
                        }
                    }
                    float val = spring_ctx.chain_count > 0 && spring_ctx.chains[0].joint_count > 0
                        ? spring_ctx.chains[0].joints[0].stiffness : 0;
                    (void)val;
                }
                if (ev.key.keysym.sym == SDLK_d || ev.key.keysym.sym == SDLK_f) {
                    float delta = (ev.key.keysym.sym == SDLK_f) ? 0.05f : -0.05f;
                    for (int ci = 0; ci < spring_ctx.chain_count; ci++) {
                        for (int ji = 0; ji < spring_ctx.chains[ci].joint_count; ji++) {
                            spring_ctx.chains[ci].joints[ji].drag_force += delta;
                            if (spring_ctx.chains[ci].joints[ji].drag_force < 0.0f)
                                spring_ctx.chains[ci].joints[ji].drag_force = 0.0f;
                            if (spring_ctx.chains[ci].joints[ji].drag_force > 1.0f)
                                spring_ctx.chains[ci].joints[ji].drag_force = 1.0f;
                        }
                    }
                    float val = spring_ctx.chain_count > 0 && spring_ctx.chains[0].joint_count > 0
                        ? spring_ctx.chains[0].joints[0].drag_force : 0;
                    (void)val;
                }
                if (ev.key.keysym.sym == SDLK_g || ev.key.keysym.sym == SDLK_h) {
                    float delta = (ev.key.keysym.sym == SDLK_h) ? 0.05f : -0.05f;
                    for (int ci = 0; ci < spring_ctx.chain_count; ci++) {
                        for (int ji = 0; ji < spring_ctx.chains[ci].joint_count; ji++) {
                            spring_ctx.chains[ci].joints[ji].gravity_power += delta;
                            if (spring_ctx.chains[ci].joints[ji].gravity_power < 0.0f)
                                spring_ctx.chains[ci].joints[ji].gravity_power = 0.0f;
                        }
                    }
                    float val = spring_ctx.chain_count > 0 && spring_ctx.chains[0].joint_count > 0
                        ? spring_ctx.chains[0].joints[0].gravity_power : 0;
                    (void)val;
                }
                if (ev.key.keysym.sym == SDLK_j || ev.key.keysym.sym == SDLK_k) {
                    float delta = (ev.key.keysym.sym == SDLK_k) ? 0.005f : -0.005f;
                    for (int ci = 0; ci < spring_ctx.chain_count; ci++) {
                        for (int ji = 0; ji < spring_ctx.chains[ci].joint_count; ji++) {
                            spring_ctx.chains[ci].joints[ji].hit_radius += delta;
                            if (spring_ctx.chains[ci].joints[ji].hit_radius < 0.0f)
                                spring_ctx.chains[ci].joints[ji].hit_radius = 0.0f;
                        }
                    }
                    float val = spring_ctx.chain_count > 0 && spring_ctx.chains[0].joint_count > 0
                        ? spring_ctx.chains[0].joints[0].hit_radius : 0;
                    (void)val;
                }
                if (ev.key.keysym.sym == SDLK_l) {
                    /* Reset spring bone params to VRM defaults */
                    for (int ci = 0; ci < spring_ctx.chain_count; ci++) {
                        if ((uint32_t)ci >= spring_ctx.model->spring_group_count) break;
                        vrm_spring_group_t *sg = &spring_ctx.model->spring_groups[ci];
                        for (int ji = 0; ji < spring_ctx.chains[ci].joint_count; ji++) {
                            if ((uint32_t)ji >= sg->joint_count) break;
                            spring_ctx.chains[ci].joints[ji].stiffness     = sg->joints[ji].stiffness;
                            spring_ctx.chains[ci].joints[ji].drag_force    = sg->joints[ji].drag_force;
                            spring_ctx.chains[ci].joints[ji].gravity_power = sg->joints[ji].gravity_power;
                            spring_ctx.chains[ci].joints[ji].hit_radius    = sg->joints[ji].hit_radius;
                        }
                    }
                    spring_ctx.initialized = 0;
                }
                /* F11: toggle fullscreen */
                if (ev.key.keysym.sym == SDLK_F11) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                        SDL_SetWindowFullscreen(window, 0);
                    } else {
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                }
                /* 1-9: raw expression toggle (for debugging) */
                if (ev.key.keysym.sym >= SDLK_1 && ev.key.keysym.sym <= SDLK_9) {
                    int expr_idx = ev.key.keysym.sym - SDLK_1;
                    if ((uint32_t)expr_idx < model.expression_count) {
                        float cur = model.expression_weights[expr_idx];
                        float nw = (cur < 0.5f) ? 1.0f : 0.0f;
                        vrm_set_expression_weight(&model, expr_idx, nw);
                    }
                }
                break;
            default: break;
            }
        }

        /* ---- handle pending animation switch request ---- */
        if (s_anim_req_pending) {
            s_anim_req_pending = 0;
            int req_return_to_idle = s_anim_req_return_to_idle;
            s_anim_req_return_to_idle = 0;
            for (int ni = 0; ni < anim_name_count; ni++) {
                if (strcmp(anim_names[ni], s_anim_req_name) == 0) {
                    START_ANIM_BLEND();
                    active_anim     = first_vrma_anim + (uint32_t)ni;
                    start_ticks     = SDL_GetTicks();
                    has_anim        = 1;
                    idle_last_pick  = ni;
                    idle_is_oneshot = req_return_to_idle || __is_oneshot(anim_names[ni]);
                    idle_next_switch = SDL_GetTicks() +
                        __rand_interval_ms(IDLE_SWITCH_MIN_SEC, IDLE_SWITCH_MAX_SEC);
                    break;
                }
            }
        }

        /* ---- handle pending emotion request ---- */
        if (s_emo_req_pending) {
            s_emo_req_pending = 0;
            int eid = emotion_find_by_name(s_emo_req_name);
            if (eid >= 0) {
                emotion_clear_overrides(&emo_ctx);
                emotion_set_intensity(&emo_ctx, s_emo_req_intensity);
                emotion_set_ex(&emo_ctx, (emotion_id_t)eid, s_emo_req_speed);
                cur_emo = (emotion_id_t)eid;
            } else {
                fprintf(stderr, "[vrm] emotion not found: %s\n", s_emo_req_name);
            }
        }

        /* ---- sync speaking state (audio thread -> render thread) ---- */
        if (s_speaking_active && !emo_ctx.speaking) {
            emotion_set_speaking(&emo_ctx, 1);
        }

        /* ---- cloud timeIndex subtitle + lip sync timeline ---- */
        if (vrm_text_timeline_is_active()) {
            char timeline_sub[768];

            lip_sync_set_timeline_sync(&s_lip_sync_ctx, 1);
            vrm_text_timeline_set_stream_ms(lip_sync_get_stream_ms(&s_lip_sync_ctx));
            vrm_text_timeline_build_visible(timeline_sub, sizeof(timeline_sub));
            vrm_overlay_set_subtitle(timeline_sub);
        } else {
            lip_sync_set_timeline_sync(&s_lip_sync_ctx, 0);
        }

        /* ---- handle delayed speaking stop (wait for ALSA buffer drain) ---- */
        if (s_speaking_stop_pending) {
            Uint32 tail_ms = SPEAKING_FADE_MS;
            int    delay_ms = s_lip_sync_ctx.delay_ms;

            if (delay_ms > 0) {
                Uint32 audio_tail = (Uint32)delay_ms + 40U;

                if (audio_tail > tail_ms) {
                    tail_ms = audio_tail;
                }
            }

            Uint32 elapsed = SDL_GetTicks() - s_speaking_stop_ticks;
            if (elapsed >= tail_ms) {
                s_speaking_stop_pending = 0;
                s_speaking_active       = 0;
                emotion_set_speaking(&emo_ctx, 0);
                if (s_subtitle_clear_pending) {
                    s_subtitle_clear_pending = 0;
                    vrm_overlay_set_subtitle("");
                    vrm_text_timeline_reset();
                }
            }
        }

        /* ---- handle pending blendshape overrides ---- */
        if (s_bs_clear_pending) {
            s_bs_clear_pending = 0;
            emotion_clear_overrides(&emo_ctx);
        }
        if (s_bs_req_pending) {
            s_bs_req_pending = 0;
            int count = s_bs_req_count;
            for (int bi = 0; bi < count; bi++) {
                int idx = vrm_find_expression(&model, s_bs_req_entries[bi].name);
                if (idx >= 0) {
                    emotion_set_override(&emo_ctx, idx, s_bs_req_entries[bi].weight);
                }
            }
            s_bs_req_count = 0;
        }

        /* ---- handle model reload ---- */
        if (s_reload_model_pending) {
            s_reload_model_pending = 0;

            /* Free GPU meshes */
            for (uint32_t mi = 0; mi < model.mesh_count; mi++) {
                mtoon_free_mesh_textures(&gpu[mi]);
                glDeleteVertexArrays(1, &gpu[mi].vao);
                glDeleteBuffers(1, &gpu[mi].vbo);
                glDeleteBuffers(1, &gpu[mi].ebo);
            }
            free(gpu);
            gpu = NULL;

            /* Free bone TBO */
            if (bone_tbo_tex) { glDeleteTextures(1, &bone_tbo_tex); bone_tbo_tex = 0; }
            if (bone_tbo_buf) { glDeleteBuffers(1, &bone_tbo_buf); bone_tbo_buf = 0; }
            free(bone_matrices); bone_matrices = NULL;
            free(target_bone_matrices); target_bone_matrices = NULL;
            free(blend_from_matrices); blend_from_matrices = NULL;

            /* Shutdown emotion & spring bones */
            s_emo_ctx = NULL;
            emotion_shutdown(&emo_ctx);
            spring_bone_shutdown(&spring_ctx);

            /* Free old model & animation names */
            vrm_model_free(&model);
            for (int ai = 0; ai < anim_name_count; ai++) {
                free(anim_names[ai]);
            }
            anim_name_count = 0;
            has_anim = 0;
            active_anim = 0;
            first_vrma_anim = 0;

            /* Load new model */
            if (vrm_model_load(&model, s_reload_model_path) != 0) {
                fprintf(stderr, "[vrm] reload failed\n");
                running = false;
                continue;
            }

            /* Reload VRMA animations from the same directory */
            if (vrma_dir && vrma_dir[0] != '\0') {
                char *vrma_files_tmp[MAX_VRMA_FILES];
                int vrma_cnt = 0;
                DIR *ddir = opendir(vrma_dir);
                if (ddir) {
                    struct dirent *de;
                    while ((de = readdir(ddir)) != NULL && vrma_cnt < MAX_VRMA_FILES) {
                        size_t nl = strlen(de->d_name);
                        if (nl > 5 && strcasecmp(de->d_name + nl - 5, ".vrma") == 0) {
                            vrma_files_tmp[vrma_cnt++] = strdup(de->d_name);
                        }
                    }
                    closedir(ddir);
                }
                if (vrma_cnt > 1) {
                    qsort(vrma_files_tmp, vrma_cnt, sizeof(char *), __cmp_str);
                }
                first_vrma_anim = model.animation_count;
                for (int vi = 0; vi < vrma_cnt; vi++) {
                    char fp[1024];
                    snprintf(fp, sizeof(fp), "%s/%s", vrma_dir, vrma_files_tmp[vi]);
                    if (vrm_load_vrma(&model, fp) == 0) {
                        char *dn = strdup(vrma_files_tmp[vi]);
                        size_t dl = strlen(dn);
                        if (dl > 5) { dn[dl - 5] = '\0'; }
                        anim_names[anim_name_count++] = dn;
                    }
                    free(vrma_files_tmp[vi]);
                }
                if (anim_name_count > 0) {
                    int idle_idx = __find_anim_idx(anim_names, anim_name_count, "idle_normal");
                    active_anim = (idle_idx >= 0)
                        ? (first_vrma_anim + (uint32_t)idle_idx)
                        : first_vrma_anim;
                    has_anim = 1;
                }
            }
            if (!has_anim && model.animation_count > 0) {
                active_anim = 0;
                has_anim = 1;
            }
            total_anims = model.animation_count;

            /* Re-upload meshes */
            gpu = (gpu_mesh_t *)calloc(model.mesh_count, sizeof(gpu_mesh_t));
            for (uint32_t mi = 0; mi < model.mesh_count; mi++) {
                __upload_mesh(&gpu[mi], &model.meshes[mi], &model, mi);
            }

            /* Recreate bone TBO */
            bone_count = model.bone_count;
            anim_blend_active = 0;
            if (bone_count > 0) {
                bone_matrices = (float *)calloc(bone_count * 16, sizeof(float));
                target_bone_matrices = (float *)calloc(bone_count * 16, sizeof(float));
                blend_from_matrices = (float *)calloc(bone_count * 16, sizeof(float));
                vrm_rest_pose_matrices(&model, bone_matrices);
                memcpy(target_bone_matrices, bone_matrices, bone_count * 16 * sizeof(float));
                memcpy(blend_from_matrices, bone_matrices, bone_count * 16 * sizeof(float));

                glGenBuffers(1, &bone_tbo_buf);
                glBindBuffer(GL_TEXTURE_BUFFER, bone_tbo_buf);
                glBufferData(GL_TEXTURE_BUFFER,
                             (GLsizeiptr)(bone_count * 16 * sizeof(float)),
                             bone_matrices, GL_DYNAMIC_DRAW);
                glGenTextures(1, &bone_tbo_tex);
                glBindTexture(GL_TEXTURE_BUFFER, bone_tbo_tex);
                glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, bone_tbo_buf);
                glBindTexture(GL_TEXTURE_BUFFER, 0);
                glBindBuffer(GL_TEXTURE_BUFFER, 0);
            }

            /* Reinit emotion & spring bones */
            emotion_init(&emo_ctx, &model);
            emotion_set_lip_sync(&emo_ctx, &s_lip_sync_ctx);
            s_emo_ctx = &emo_ctx;
            spring_bone_init(&spring_ctx, &model);

            /* Recalculate morph info */
            total_morph_targets = 0;
            for (uint32_t mi = 0; mi < model.mesh_count; mi++) {
                total_morph_targets += (int)model.meshes[mi].morph_target_count;
            }

            /* Re-detect facing for the new model */
            model_y_rot_base = __detect_model_facing(&model);
            model_y_rot = model_y_rot_base;
            model_x_rot = 0.0f;

            /* Reset camera to new model */
            cam_yaw = 0.0f;
            cam_pitch = 0.15f;
            cam_dist = model.extent * 2.0f;
            if (cam_dist < 1.0f) { cam_dist = 3.0f; }
            cam_target[0] = model.center[0];
            cam_target[1] = model.center[1];
            cam_target[2] = model.center[2];

            vrm_overlay_update_anims(anim_names, anim_name_count, 0);

            start_ticks = SDL_GetTicks();
            s_last_interact_ticks = SDL_GetTicks();
            idle_last_pick = -1;
            idle_is_oneshot = 0;
            idle_next_switch = SDL_GetTicks() +
                __rand_interval_ms(IDLE_SWITCH_MIN_SEC, IDLE_SWITCH_MAX_SEC);
            idle_next_emotion = SDL_GetTicks() +
                __rand_interval_ms(IDLE_EMOTION_MIN_SEC, IDLE_EMOTION_MAX_SEC);

            continue;
        }

        /* ---- handle scene reload ---- */
        if (s_reload_scene_pending) {
            s_reload_scene_pending = 0;
            __load_scene_background(&skybox, &bg2d, s_reload_scene_dir);
        }

        /* ---- sync animation selection from LVGL settings ---- */
        if (s_sui_anim_req_pending && anim_name_count > 0) {
            s_sui_anim_req_pending = 0;
            int idx = s_sui_anim_req_idx;
            if (idx >= 0 && idx < anim_name_count) {
                START_ANIM_BLEND();
                active_anim     = first_vrma_anim + (uint32_t)idx;
                start_ticks     = SDL_GetTicks();
                has_anim        = 1;
                idle_last_pick  = idx;
                idle_is_oneshot = __is_oneshot(anim_names[idx]);
                idle_next_switch = SDL_GetTicks() +
                    __rand_interval_ms(IDLE_SWITCH_MIN_SEC, IDLE_SWITCH_MAX_SEC);
                s_last_interact_ticks = SDL_GetTicks();
            }
        }

        /* ---- idle behavior engine ---- */
        if (has_anim && anim_name_count > 0 && !paused) {
            Uint32 idle_now = SDL_GetTicks();
            float since_interact = (float)(idle_now - s_last_interact_ticks) / 1000.0f;

            /* A. One-shot completion: auto-return to idle_normal after playback */
            if (idle_is_oneshot && active_anim < total_anims) {
                float dur = model.animations[active_anim].duration;
                float elapsed = (float)(idle_now - start_ticks) / 1000.0f;
                if (elapsed >= dur) {
                    int idx = __pick_recovery_idle_anim(anim_names, anim_name_count,
                                                        idle_last_pick);
                    if (idx >= 0) {
                        START_ANIM_BLEND();
                        active_anim     = first_vrma_anim + (uint32_t)idx;
                        start_ticks     = idle_now;
                        idle_last_pick  = idx;
                        idle_is_oneshot = __is_oneshot(anim_names[idx]);
                        idle_next_switch = idle_now +
                            __rand_interval_ms(IDLE_SWITCH_MIN_SEC, IDLE_SWITCH_MAX_SEC);
                    }
                }
            }

            /* B. Idle mode: animation rotation + random emotions */
            int idle_mode = (since_interact > IDLE_INTERACT_COOLDOWN && !emo_ctx.speaking);

            if (idle_mode && !idle_was_active) {
                idle_next_switch  = idle_now +
                    __rand_interval_ms(IDLE_SWITCH_MIN_SEC, IDLE_SWITCH_MAX_SEC);
                idle_next_emotion = idle_now +
                    __rand_interval_ms(IDLE_EMOTION_MIN_SEC, IDLE_EMOTION_MAX_SEC);
                idle_emotion_revert = 0;
            }

            if (!idle_mode && idle_was_active) {
                if (cur_emo != EMOTION_NEUTRAL) {
                    emotion_set(&emo_ctx, EMOTION_NEUTRAL);
                    cur_emo = EMOTION_NEUTRAL;
                }
                idle_emotion_revert = 0;
            }

            idle_was_active = idle_mode;

            if (idle_mode) {
                /* Idle animation rotation */
                if (!idle_is_oneshot && (int32_t)(idle_now - idle_next_switch) >= 0) {
                    int idx = __pick_idle_anim(anim_names, anim_name_count, idle_last_pick);
                    if (idx >= 0) {
                        idle_next_switch = idle_now +
                            __rand_interval_ms(IDLE_SWITCH_MIN_SEC, IDLE_SWITCH_MAX_SEC);
                        uint32_t next_anim = first_vrma_anim + (uint32_t)idx;
                        if (active_anim != next_anim) {
                            START_ANIM_BLEND();
                            active_anim     = next_anim;
                            start_ticks     = idle_now;
                            idle_last_pick  = idx;
                            idle_is_oneshot = __is_oneshot(anim_names[idx]);
                        }
                    }
                }

                /* Random emotion micro-variation */
                if ((int32_t)(idle_now - idle_next_emotion) >= 0) {
                    emotion_id_t eid = s_idle_emotions[rand() % IDLE_EMOTION_COUNT];
                    if (eid != cur_emo) {
                        emotion_set(&emo_ctx, eid);
                        cur_emo = eid;
                        if (eid != EMOTION_NEUTRAL) {
                            idle_emotion_revert = idle_now +
                                (Uint32)(IDLE_EMOTION_HOLD_SEC * 1000);
                        }
                    }
                    idle_next_emotion = idle_now +
                        __rand_interval_ms(IDLE_EMOTION_MIN_SEC, IDLE_EMOTION_MAX_SEC);
                }

                /* Revert non-neutral emotion after hold time */
                if (idle_emotion_revert > 0 &&
                    (int32_t)(idle_now - idle_emotion_revert) >= 0) {
                    if (cur_emo != EMOTION_NEUTRAL) {
                        emotion_set(&emo_ctx, EMOTION_NEUTRAL);
                        cur_emo = EMOTION_NEUTRAL;
                    }
                    idle_emotion_revert = 0;
                }
            }
        }

        /* ---- animation time ---- */
        float anim_time = 0.0f;
        if (!paused) {
            anim_time = (float)(SDL_GetTicks() - start_ticks) / 1000.0f;
        }

        /* ---- update bone matrices ---- */
        if (bone_count > 0) {
            float *evaluated_matrices = target_bone_matrices ? target_bone_matrices : bone_matrices;
            if (has_anim) {
                vrm_evaluate_animation(&model, active_anim, anim_time, evaluated_matrices);
            } else {
                vrm_rest_pose_matrices(&model, evaluated_matrices);
            }

            if (evaluated_matrices != bone_matrices) {
                if (anim_blend_active && blend_from_matrices) {
                    float blend_t = (float)(SDL_GetTicks() - anim_blend_start_ticks) /
                                    (ANIM_SWITCH_BLEND_SEC * 1000.0f);
                    if (blend_t >= 1.0f) {
                        memcpy(bone_matrices, evaluated_matrices,
                               bone_count * 16 * sizeof(float));
                        anim_blend_active = 0;
                    } else {
                        float smooth_t = blend_t * blend_t * (3.0f - 2.0f * blend_t);
                        for (uint32_t mi = 0; mi < bone_count * 16; mi++) {
                            bone_matrices[mi] = blend_from_matrices[mi] +
                                (evaluated_matrices[mi] - blend_from_matrices[mi]) * smooth_t;
                        }
                    }
                } else {
                    memcpy(bone_matrices, evaluated_matrices,
                           bone_count * 16 * sizeof(float));
                }
            }

            /* ---- Spring bone physics ---- */
            {
                Uint32 now = SDL_GetTicks();
                float sdt = (float)(now - last_frame_ticks) / 1000.0f;
                if (sdt <= 0.0f) sdt = 0.016f;
                if (sdt > 0.1f) sdt = 0.016f;
                spring_bone_update(&spring_ctx, sdt, bone_matrices);
            }

            /* Upload to TBO */
            glBindBuffer(GL_TEXTURE_BUFFER, bone_tbo_buf);
            glBufferSubData(GL_TEXTURE_BUFFER, 0,
                            (GLsizeiptr)(bone_count * 16 * sizeof(float)),
                            bone_matrices);
            glBindBuffer(GL_TEXTURE_BUFFER, 0);
        }

        /* ---- Emotion & BlendShape / morph target update ---- */
        {
            Uint32 now_ticks = SDL_GetTicks();
            float frame_dt = (float)(now_ticks - last_frame_ticks) / 1000.0f;
            if (frame_dt > 0.1f) frame_dt = 0.1f; /* clamp large spikes */
            last_frame_ticks = now_ticks;

            /* Drive expression weights via emotion system */
            emotion_update(&emo_ctx, frame_dt);

            /* Apply morph target deltas to CPU vertex data */
            if (total_morph_targets > 0) {
                vrm_apply_morph_targets(&model);

                /* Re-upload VBOs for meshes with morph targets */
                for (uint32_t i = 0; i < model.mesh_count; i++) {
                    if (gpu[i].has_morph) {
                        glBindBuffer(GL_ARRAY_BUFFER, gpu[i].vbo);
                        glBufferSubData(GL_ARRAY_BUFFER, 0,
                                        (GLsizeiptr)(gpu[i].vertex_count * 16 * sizeof(float)),
                                        model.meshes[i].vertices);
                    }
                }
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            }
        }

        /* ---- model matrix (drag rotation) ---- */
        mat4 view_mat, proj_mat, model_mat, vp, mvp;
        {
            mat4 ry;
            mat4_rotate_y(ry, model_y_rot);
            if (model_x_rot != 0.0f) {
                mat4 rx;
                mat4_rotate_x(rx, model_x_rot);
                mat4_multiply(model_mat, rx, ry);
            } else {
                memcpy(model_mat, ry, sizeof(mat4));
            }
        }

        /* Recompute camera target from rotated model center */
        cam_target[0] = model_mat[0] * model.center[0] + model_mat[4] * model.center[1] + model_mat[8]  * model.center[2];
        cam_target[1] = model_mat[1] * model.center[0] + model_mat[5] * model.center[1] + model_mat[9]  * model.center[2];
        cam_target[2] = model_mat[2] * model.center[0] + model_mat[6] * model.center[1] + model_mat[10] * model.center[2];

        /* ---- camera ---- */
        float eye[3];
        float cam_look_target[3];
        float up[3] = { 0.0f, 1.0f, 0.0f };

        int spectator_on = vrm_overlay_spectator();
        if (spectator_on) {
            if (!s_spec_inited) {
                s_spec_yaw   = cam_yaw;
                s_spec_pitch = cam_pitch;
                s_spec_dist  = cam_dist;
                s_spec_target[0] = cam_target[0];
                s_spec_target[1] = cam_target[1];
                s_spec_target[2] = cam_target[2];
                s_spec_inited = 1;
            }

            eye[0] = s_spec_target[0] + s_spec_dist * cosf(s_spec_pitch) * sinf(s_spec_yaw);
            eye[1] = s_spec_target[1] + s_spec_dist * sinf(s_spec_pitch);
            eye[2] = s_spec_target[2] + s_spec_dist * cosf(s_spec_pitch) * cosf(s_spec_yaw);
            cam_look_target[0] = s_spec_target[0];
            cam_look_target[1] = s_spec_target[1];
            cam_look_target[2] = s_spec_target[2];
        } else {
            eye[0] = cam_target[0] + cam_dist * cosf(cam_pitch) * sinf(cam_yaw);
            eye[1] = cam_target[1] + cam_dist * sinf(cam_pitch);
            eye[2] = cam_target[2] + cam_dist * cosf(cam_pitch) * cosf(cam_yaw);
            cam_look_target[0] = cam_target[0];
            cam_look_target[1] = cam_target[1];
            cam_look_target[2] = cam_target[2];
        }

        mat4_look_at(view_mat, eye, cam_look_target, up);
        mat4_perspective(proj_mat, 45.0f * (float)M_PI / 180.0f,
                         (float)win_w / (float)(win_h > 0 ? win_h : 1),
                         0.01f, 1000.0f);

        mat4_multiply(vp, proj_mat, view_mat);
        mat4_multiply(mvp, vp, model_mat);

        /* ---- Light matrix for shadow mapping ---- */
        mat4 light_vp, light_mvp;
        {
            float le = model.extent * 1.5f;
            if (le < 1.0f) le = 1.0f;
            vec3 l_eye = {
                cam_target[0],
                cam_target[1] + le * 2.0f,
                cam_target[2] + le * 2.5f
            };
            vec3 l_at = { cam_target[0], model.bbox_min[1], cam_target[2] };
            vec3 l_up = { 0.0f, 0.0f, -1.0f };
            mat4 lv, lp;
            mat4_look_at(lv, l_eye, l_at, l_up);
            mat4_ortho(lp, -le, le, -le, le, 0.01f, le * 6.0f);
            mat4_multiply(light_vp, lp, lv);
            mat4_multiply(light_mvp, light_vp, model_mat);
        }

        /* ---- Shadow depth pass ---- */
        glBindFramebuffer(GL_FRAMEBUFFER, s_shadow_fbo);
        glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glUseProgram(shadow_prog);
        glUniformMatrix4fv(glGetUniformLocation(shadow_prog, "u_light_mvp"),
                           1, GL_FALSE, light_mvp);
        glUniform1i(glGetUniformLocation(shadow_prog, "u_bone_tbo"), 1);
        if (bone_tbo_tex) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_BUFFER, bone_tbo_tex);
        }
        for (uint32_t i = 0; i < model.mesh_count; i++) {
            glUniform1i(glGetUniformLocation(shadow_prog, "u_skinned"),
                        gpu[i].has_bones);
            glBindVertexArray(gpu[i].vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)gpu[i].index_count,
                           GL_UNSIGNED_INT, 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glViewport(0, 0, win_w, win_h);

        /* ---- draw background ---- */
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (skybox.loaded) {
            skybox_draw(&skybox, view_mat, proj_mat);
        } else if (bg2d.loaded) {
            bg2d_draw(&bg2d, win_w, win_h);
        } else {
            glDisable(GL_DEPTH_TEST);
            glUseProgram(bg_prog);
            glBindVertexArray(s_bg_vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glEnable(GL_DEPTH_TEST);
        }

        /* ---- draw grid only when no scene background is active ---- */
        if (!skybox.loaded && !bg2d.loaded) {
            glUseProgram(grid_prog);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            {
                mat4 gm, gmvp;
                mat4_scale_uniform(gm, model.extent * 1.5f);
                gm[13] = model.bbox_min[1];
                mat4_multiply(gmvp, vp, gm);
                glUniformMatrix4fv(glGetUniformLocation(grid_prog, "u_mvp"), 1, GL_FALSE, gmvp);
            }
            glBindVertexArray(s_grid_vao);
            glDrawArrays(GL_LINES, 0, s_grid_vert_count);
            glDisable(GL_BLEND);
        }

        /* ---- draw model (MToon + legacy fallback) ---- */
        {
            float ld[3] = { 0.0f, 0.15f, 1.0f };
            float len = sqrtf(ld[0]*ld[0] + ld[1]*ld[1] + ld[2]*ld[2]);
            ld[0] /= len; ld[1] /= len; ld[2] /= len;
            mtoon_draw_model(&model, gpu, mtoon_prog, model_prog, outline_prog,
                             mvp, model_mat, ld, eye, bone_tbo_tex, white_tex);
        }

        /* ---- Ground shadow ---- */
        glUseProgram(ground_shadow_prog);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        {
            float gs = model.extent * 3.0f;
            mat4 ground_mat;
            mat4_identity(ground_mat);
            ground_mat[0]  = gs;
            ground_mat[10] = gs;
            ground_mat[13] = model.bbox_min[1];

            mat4 gmvp;
            mat4_multiply(gmvp, vp, ground_mat);

            glUniformMatrix4fv(
                glGetUniformLocation(ground_shadow_prog, "u_mvp"),
                1, GL_FALSE, gmvp);
            glUniformMatrix4fv(
                glGetUniformLocation(ground_shadow_prog, "u_light_vp"),
                1, GL_FALSE, light_vp);
            glUniformMatrix4fv(
                glGetUniformLocation(ground_shadow_prog, "u_ground_mat"),
                1, GL_FALSE, ground_mat);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_shadow_depth_tex);
            glUniform1i(
                glGetUniformLocation(ground_shadow_prog, "u_shadow_map"), 0);

            vec3 sc = { cam_target[0], 0.0f, cam_target[2] };
            glUniform3fv(
                glGetUniformLocation(ground_shadow_prog, "u_center"), 1, sc);
            glUniform1f(
                glGetUniformLocation(ground_shadow_prog, "u_radius"),
                gs * 0.8f);

            glBindVertexArray(s_ground_vao);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        /* ---- Settings overlay + subtitle (2D on top of 3D) ---- */
        vrm_overlay_render(win_w, win_h);

        SDL_GL_SwapWindow(window);
    }

    /* ---- cleanup ---- */
    for (uint32_t i = 0; i < model.mesh_count; i++) {
        mtoon_free_mesh_textures(&gpu[i]);
        glDeleteVertexArrays(1, &gpu[i].vao);
        glDeleteBuffers(1, &gpu[i].vbo);
        glDeleteBuffers(1, &gpu[i].ebo);
    }
    free(gpu);
    free(bone_matrices);
    free(target_bone_matrices);
    free(blend_from_matrices);

    if (bone_tbo_tex) glDeleteTextures(1, &bone_tbo_tex);
    if (bone_tbo_buf) glDeleteBuffers(1, &bone_tbo_buf);

    glDeleteTextures(1, &white_tex);
    glDeleteVertexArrays(1, &s_grid_vao);
    glDeleteBuffers(1, &s_grid_vbo);
    glDeleteVertexArrays(1, &s_bg_vao);
    glDeleteBuffers(1, &s_bg_vbo);
    glDeleteFramebuffers(1, &s_shadow_fbo);
    glDeleteTextures(1, &s_shadow_depth_tex);
    glDeleteVertexArrays(1, &s_ground_vao);
    glDeleteBuffers(1, &s_ground_vbo);
    glDeleteBuffers(1, &s_ground_ebo);
    glDeleteProgram(model_prog);
    if (mtoon_prog) {
        glDeleteProgram(mtoon_prog);
    }
    if (outline_prog) {
        glDeleteProgram(outline_prog);
    }
    glDeleteProgram(grid_prog);
    glDeleteProgram(bg_prog);
    glDeleteProgram(shadow_prog);
    glDeleteProgram(ground_shadow_prog);
    skybox_destroy(&skybox);
    bg2d_destroy(&bg2d);

    vrm_overlay_destroy();

    /* Nullify global emotion ctx before shutdown to prevent dangling access */
    s_emo_ctx = NULL;
    /* Restore default speaker consumer */
    tuya_ai_player_set_consumer(NULL);

    emotion_shutdown(&emo_ctx);
    spring_bone_shutdown(&spring_ctx);
    vrm_model_free(&model);

    for (int i = 0; i < anim_name_count; i++)
        free(anim_names[i]);

    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;

#undef START_ANIM_BLEND
}
