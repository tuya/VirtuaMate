/**
 * @file bg2d.c
 * @brief Full-screen 2D background image — loads a single image from a scene folder.
 * @version 1.0
 * @date 2025-06-29
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#include "vrm_bg2d.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stb/stb_image.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define BG2D_PATH_MAX 1024

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
static const char *s_bg2d_vs =
    "#version 140\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location=0) in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.999, 1.0);\n"
    "    v_uv = a_pos * 0.5 + 0.5;\n"
    "}\n";

static const char *s_bg2d_fs =
    "#version 140\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_cover;\n"
    "void main() {\n"
    "    vec2 uv = v_uv * u_cover.xy + u_cover.zw;\n"
    "    frag_color = texture(u_tex, uv);\n"
    "}\n";

static const char *s_preferred_names[] = {
    "back", "background", "bg", NULL
};

static const char *s_extensions[] = {
    ".jpg", ".jpeg", ".png", ".bmp", ".tga", ".webp", NULL
};

static const char *s_skip_names[] = {
    "cubemap_layout", NULL
};

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Compile a single shader stage
 * @param[in] type  GL_VERTEX_SHADER or GL_FRAGMENT_SHADER
 * @param[in] src   GLSL source string
 * @return shader handle, or 0 on failure
 */
static GLuint __compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[bg2d] shader compile error:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/**
 * @brief Link a vertex + fragment shader into a program
 * @param[in] vs_src  Vertex shader source
 * @param[in] fs_src  Fragment shader source
 * @return program handle, or 0 on failure
 */
static GLuint __link_program(const char *vs_src, const char *fs_src)
{
    GLuint vs = __compile_shader(GL_VERTEX_SHADER, vs_src);
    if (!vs) {
        return 0;
    }
    GLuint fs = __compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    int ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "[bg2d] program link error:\n%s\n", log);
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

/**
 * @brief Check whether a base filename should be skipped for 2D background scan
 * @param[in] base  Filename without extension
 * @return 1 if the name should be skipped, 0 otherwise
 */
static int __should_skip_name(const char *base)
{
    for (int i = 0; s_skip_names[i] != NULL; i++) {
        if (strcmp(base, s_skip_names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Test whether a path points to a readable image file
 * @param[in] path  File path to test
 * @return 1 if readable, 0 otherwise
 */
static int __file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/**
 * @brief Try preferred base names inside a scene directory
 * @param[out] out_path  Buffer to receive the found path
 * @param[in]  out_size  Size of out_path buffer
 * @param[in]  dir       Scene directory path
 * @return 0 if found, -1 otherwise
 */
static int __find_preferred_image(char *out_path, size_t out_size, const char *dir)
{
    for (int n = 0; s_preferred_names[n] != NULL; n++) {
        for (int e = 0; s_extensions[e] != NULL; e++) {
            snprintf(out_path, out_size, "%s/%s%s",
                     dir, s_preferred_names[n], s_extensions[e]);
            if (__file_exists(out_path)) {
                return 0;
            }
        }
    }
    return -1;
}

/**
 * @brief Extract lowercase extension from a filename
 * @param[out] out_ext  Buffer for extension including dot
 * @param[in]  out_size Size of out_ext buffer
 * @param[in]  name     Filename
 * @return none
 */
static void __get_ext(char *out_ext, size_t out_size, const char *name)
{
    out_ext[0] = '\0';
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) {
        return;
    }
    snprintf(out_ext, out_size, "%s", dot);
    for (char *p = out_ext; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
}

/**
 * @brief Return 1 if extension is a supported image type
 * @param[in] ext  Extension string including dot
 * @return 1 if supported, 0 otherwise
 */
static int __is_image_ext(const char *ext)
{
    for (int e = 0; s_extensions[e] != NULL; e++) {
        if (strcmp(ext, s_extensions[e]) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Scan directory for a single usable background image
 * @param[out] out_path  Buffer to receive the found path
 * @param[in]  out_size  Size of out_path buffer
 * @param[in]  dir       Scene directory path
 * @return 0 if found, -1 otherwise
 */
static int __find_any_image(char *out_path, size_t out_size, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }

    char found[BG2D_PATH_MAX];
    int found_count = 0;
    found[0] = '\0';

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }

        char ext[16];
        __get_ext(ext, sizeof(ext), ent->d_name);
        if (!__is_image_ext(ext)) {
            continue;
        }

        char base[256];
        size_t name_len = strlen(ent->d_name);
        size_t ext_len = strlen(ext);
        if (name_len <= ext_len) {
            continue;
        }
        snprintf(base, sizeof(base), "%.*s",
                 (int)(name_len - ext_len), ent->d_name);
        if (__should_skip_name(base)) {
            continue;
        }

        snprintf(found, sizeof(found), "%s/%s", dir, ent->d_name);
        found_count++;
        if (found_count > 1) {
            closedir(d);
            return -1;
        }
    }

    closedir(d);
    if (found_count != 1) {
        return -1;
    }

    strncpy(out_path, found, out_size - 1);
    out_path[out_size - 1] = '\0';
    return 0;
}

/**
 * @brief Locate a 2D background image inside a scene directory
 * @param[out] out_path  Buffer to receive the found path
 * @param[in]  out_size  Size of out_path buffer
 * @param[in]  dir       Scene directory path
 * @return 0 if found, -1 otherwise
 */
static int __find_bg_image(char *out_path, size_t out_size, const char *dir)
{
    if (__find_preferred_image(out_path, out_size, dir) == 0) {
        return 0;
    }
    return __find_any_image(out_path, out_size, dir);
}

/**
 * @brief Initialize a 2D background from a scene directory
 * @param[out] bg2d  Background context to initialize
 * @param[in]  dir   Path to the scene directory
 * @return 0 on success, -1 on failure
 */
int bg2d_init(bg2d_t *bg2d, const char *dir)
{
    if (!bg2d || !dir || dir[0] == '\0') {
        return -1;
    }

    memset(bg2d, 0, sizeof(bg2d_t));

    char path[BG2D_PATH_MAX];
    if (__find_bg_image(path, sizeof(path), dir) != 0) {
        return -1;
    }

    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);
    unsigned char *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "[bg2d] failed to load: %s\n", path);
        return -1;
    }

    glGenTextures(1, &bg2d->tex);
    glBindTexture(GL_TEXTURE_2D, bg2d->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);

    float verts[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    glGenVertexArrays(1, &bg2d->vao);
    glGenBuffers(1, &bg2d->vbo);
    glBindVertexArray(bg2d->vao);
    glBindBuffer(GL_ARRAY_BUFFER, bg2d->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    bg2d->program = __link_program(s_bg2d_vs, s_bg2d_fs);
    if (!bg2d->program) {
        bg2d_destroy(bg2d);
        return -1;
    }

    bg2d->u_tex = glGetUniformLocation(bg2d->program, "u_tex");
    bg2d->u_cover = glGetUniformLocation(bg2d->program, "u_cover");
    glUseProgram(bg2d->program);
    glUniform1i(bg2d->u_tex, 0);
    glUseProgram(0);

    bg2d->width = w;
    bg2d->height = h;
    bg2d->loaded = 1;
    printf("[bg2d] loaded 2D background: %s (%dx%d)\n", path, w, h);
    return 0;
}

/**
 * @brief Draw the 2D background covering the full viewport
 * @param[in] bg2d    Background context (must be loaded)
 * @param[in] win_w   Viewport width in pixels
 * @param[in] win_h   Viewport height in pixels
 * @return none
 */
void bg2d_draw(const bg2d_t *bg2d, int win_w, int win_h)
{
    if (!bg2d || !bg2d->loaded || win_w <= 0 || win_h <= 0) {
        return;
    }

    float win_aspect = (float)win_w / (float)win_h;
    float img_aspect = (float)bg2d->width / (float)bg2d->height;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    if (img_aspect > win_aspect) {
        scale_x = win_aspect / img_aspect;
        offset_x = (1.0f - scale_x) * 0.5f;
    } else {
        scale_y = img_aspect / win_aspect;
        offset_y = (1.0f - scale_y) * 0.5f;
    }

    glDisable(GL_DEPTH_TEST);
    glUseProgram(bg2d->program);
    glUniform4f(bg2d->u_cover, scale_x, scale_y, offset_x, offset_y);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bg2d->tex);
    glBindVertexArray(bg2d->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}

/**
 * @brief Release all GPU resources held by the 2D background
 * @param[in] bg2d  Background context
 * @return none
 */
void bg2d_destroy(bg2d_t *bg2d)
{
    if (!bg2d) {
        return;
    }
    if (bg2d->tex) {
        glDeleteTextures(1, &bg2d->tex);
    }
    if (bg2d->vao) {
        glDeleteVertexArrays(1, &bg2d->vao);
    }
    if (bg2d->vbo) {
        glDeleteBuffers(1, &bg2d->vbo);
    }
    if (bg2d->program) {
        glDeleteProgram(bg2d->program);
    }
    memset(bg2d, 0, sizeof(bg2d_t));
}
