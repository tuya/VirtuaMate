/**
 * @file vrm_bg2d.h
 * @brief Full-screen 2D background image renderer for the VRM viewer.
 * @version 1.0
 * @date 2025-06-29
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#ifndef VRM_BG2D_H
#define VRM_BG2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <GL/glew.h>

typedef struct {
    GLuint tex;
    GLuint vao;
    GLuint vbo;
    GLuint program;
    GLint  u_tex;
    GLint  u_cover;
    int    width;
    int    height;
    int    loaded;
} bg2d_t;

/**
 * @brief Load a 2D background image from a scene directory
 * @param[out] bg2d  Background context to initialize
 * @param[in]  dir   Path to the scene directory
 * @return 0 on success, -1 on failure
 */
int bg2d_init(bg2d_t *bg2d, const char *dir);

/**
 * @brief Draw the 2D background covering the full viewport
 * @param[in] bg2d    Background context (must be loaded)
 * @param[in] win_w   Viewport width in pixels
 * @param[in] win_h   Viewport height in pixels
 * @return none
 */
void bg2d_draw(const bg2d_t *bg2d, int win_w, int win_h);

/**
 * @brief Release all GPU resources held by the 2D background
 * @param[in] bg2d  Background context
 * @return none
 */
void bg2d_destroy(bg2d_t *bg2d);

#ifdef __cplusplus
}
#endif

#endif /* VRM_BG2D_H */
