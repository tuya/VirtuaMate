/**
 * @file overlay_font.c
 * @brief Settings overlay submodule (split from settings_overlay.c)
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "overlay_internal.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype_htcw.h"

#include <GL/glew.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

static stbtt_fontinfo  s_sub_font;
static unsigned char  *s_sub_font_buf   = NULL;
static int             s_sub_font_ready = 0;
static int             s_sub_font_tried = 0;

void __overlay_font_destroy(void)
{
    free(s_sub_font_buf);
    s_sub_font_buf = NULL;
    s_sub_font_ready = 0;
    s_sub_font_tried = 0;
}

static int __file_read_all(const char *path, unsigned char **out, size_t *out_size)
{
    *out = NULL;
    *out_size = 0;
    if (!path || path[0] == '\0') {
        return 0;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    long sz = ftell(fp);
    if (sz <= 0) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    *out = buf;
    *out_size = (size_t)sz;
    return 1;
}

static int __path_exists(const char *path)
{
    return path && access(path, R_OK) == 0;
}

static int __find_repo_font_from(char *base, char *out_path, size_t out_sz)
{
    char dir[OVL_MAX_PATH];
    strncpy(dir, base, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    for (int depth = 0; depth < 8; depth++) {
        char cand[OVL_MAX_PATH];
        snprintf(cand, sizeof(cand), "%s/%s", dir, OVL_SUB_REPO_FONT);
        if (__path_exists(cand)) {
            strncpy(out_path, cand, out_sz - 1);
            out_path[out_sz - 1] = '\0';
            return 1;
        }

        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) {
            break;
        }
        *slash = '\0';
    }
    return 0;
}

static int __locate_subtitle_font(char *out_path, size_t out_sz, int *out_index)
{
    const char *env = getenv("VIRTUAMATE_SUBTITLE_FONT");
    if (env && env[0] != '\0' && __path_exists(env)) {
        strncpy(out_path, env, out_sz - 1);
        out_path[out_sz - 1] = '\0';
        *out_index = 0;
        printf("[ovl] subtitle font (env): %s\n", out_path);
        return 1;
    }

    /* Search from CWD upward */
    char cwd[OVL_MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)) && __find_repo_font_from(cwd, out_path, out_sz)) {
        *out_index = 0;
        printf("[ovl] subtitle font (cwd): %s\n", out_path);
        return 1;
    }

    /* Search from executable's directory upward (/proc/self/exe on Linux) */
    char exe_dir[OVL_MAX_PATH];
    ssize_t elen = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (elen > 0) {
        exe_dir[elen] = '\0';
        char *slash = strrchr(exe_dir, '/');
        if (slash) {
            *slash = '\0';
        }
        if (__find_repo_font_from(exe_dir, out_path, out_sz)) {
            *out_index = 0;
            printf("[ovl] subtitle font (exe): %s\n", out_path);
            return 1;
        }
    }

    /* System font fallbacks — prefer .ttf (TrueType) over .ttc (CFF) */
    static const char *sys_fonts[] = {
        "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansSC-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/TTF/NotoSansCJK-Regular.ttc",
        NULL
    };
    for (int i = 0; sys_fonts[i]; i++) {
        if (__path_exists(sys_fonts[i])) {
            strncpy(out_path, sys_fonts[i], out_sz - 1);
            out_path[out_sz - 1] = '\0';
            /* .ttc collections: index 2 = SC; .ttf files: index 0 */
            const char *ext = strrchr(sys_fonts[i], '.');
            *out_index = (ext && strcmp(ext, ".ttc") == 0) ? 2 : 0;
            printf("[ovl] subtitle font (sys): %s (index=%d)\n", out_path, *out_index);
            return 1;
        }
    }

    fprintf(stderr, "[ovl] subtitle font not found in any search path\n");
    return 0;
}

int __subtitle_font_init(void)
{
    if (s_sub_font_ready) {
        return 1;
    }
    if (s_sub_font_tried) {
        return 0;
    }
    s_sub_font_tried = 1;

    char path[OVL_MAX_PATH];
    int font_index = 0;
    if (!__locate_subtitle_font(path, sizeof(path), &font_index)) {
        fprintf(stderr, "[ovl] subtitle font not found — "
                "set VIRTUAMATE_SUBTITLE_FONT=/path/to/font.ttf\n");
        return 0;
    }

    unsigned char *buf = NULL;
    size_t buf_sz = 0;
    if (!__file_read_all(path, &buf, &buf_sz)) {
        fprintf(stderr, "[ovl] failed to read subtitle font: %s\n", path);
        return 0;
    }

    printf("[ovl] loaded font file: %s (%zu bytes, index=%d)\n",
           path, buf_sz, font_index);

    int offset = stbtt_GetFontOffsetForIndex(buf, font_index);
    if (offset < 0) {
        fprintf(stderr, "[ovl] font index %d out of range in %s "
                "(try index 0)\n", font_index, path);
        free(buf);
        return 0;
    }

    if (!stbtt_InitFont(&s_sub_font, buf, offset)) {
        fprintf(stderr, "[ovl] stbtt_InitFont failed for %s (index=%d, offset=%d)\n",
                path, font_index, offset);
        free(buf);
        return 0;
    }

    s_sub_font_buf = buf;
    s_sub_font_ready = 1;
    printf("[settings_overlay] subtitle font ready: %s (index=%d)\n",
           path, font_index);
    return 1;
}

static unsigned int __utf8_next(const char **pp)
{
    const unsigned char *s = (const unsigned char *)(*pp);
    unsigned int cp = 0xfffdU;

    if (!s || s[0] == '\0') {
        return 0;
    }

    if (s[0] < 0x80) {
        cp = s[0];
        *pp += 1;
        return cp;
    }
    if ((s[0] & 0xe0) == 0xc0 && s[1]) {
        cp = ((unsigned int)(s[0] & 0x1f) << 6) |
             (unsigned int)(s[1] & 0x3f);
        *pp += 2;
        return cp;
    }
    if ((s[0] & 0xf0) == 0xe0 && s[1] && s[2]) {
        cp = ((unsigned int)(s[0] & 0x0f) << 12) |
             ((unsigned int)(s[1] & 0x3f) << 6) |
             (unsigned int)(s[2] & 0x3f);
        *pp += 3;
        return cp;
    }
    if ((s[0] & 0xf8) == 0xf0 && s[1] && s[2] && s[3]) {
        cp = ((unsigned int)(s[0] & 0x07) << 18) |
             ((unsigned int)(s[1] & 0x3f) << 12) |
             ((unsigned int)(s[2] & 0x3f) << 6) |
             (unsigned int)(s[3] & 0x3f);
        *pp += 4;
        return cp;
    }

    *pp += 1;
    return cp;
}

void __subtitle_texture_reset(void)
{
    s_sub_draw_w = 0;
    s_sub_draw_h = 0;
    s_sub_last_max_w = -1;
    s_sub_dirty = 1;
}

static int __ensure_sub_pixel_capacity(size_t need)
{
    if (need <= s_sub_pixels_cap) {
        return 1;
    }
    unsigned char *tmp = (unsigned char *)realloc(s_sub_pixels, need);
    if (!tmp) {
        return 0;
    }
    s_sub_pixels = tmp;
    s_sub_pixels_cap = need;
    return 1;
}

static int __subtitle_build_layout(int max_w, ovl_glyph_layout_t *glyphs,
                                   int *glyph_count, int *out_w, int *out_h,
                                   int *out_baseline)
{
    if (!__subtitle_font_init()) {
        return 0;
    }

    const char *p = s_subtitle;
    float scale = stbtt_ScaleForPixelHeight(&s_sub_font, OVL_SUB_FONT_PX);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&s_sub_font, &ascent, &descent, &line_gap);
    (void)line_gap;
    int baseline = OVL_ICEILF((float)ascent * scale);
    int line_h = OVL_ICEILF((float)(ascent - descent) * scale);
    int line_step = line_h + OVL_SUB_LINE_GAP;

    int line_min[OVL_SUB_MAX_WRAP_LINES];
    int line_max[OVL_SUB_MAX_WRAP_LINES];
    for (int i = 0; i < OVL_SUB_MAX_WRAP_LINES; i++) {
        line_min[i] = 0;
        line_max[i] = 0;
    }

    int line = 0;
    int pen_x = 0;
    int prev_cp = 0;
    int count = 0;

    while (*p && count < OVL_SUB_MAX_GLYPHS) {
        unsigned int cp = __utf8_next(&p);
        if (cp == 0) {
            break;
        }
        if (cp == '\r' || cp == '\n' || cp == '\t') {
            cp = ' ';
        }

        if (cp == ' ' && pen_x == 0) {
            continue;
        }

        int kern = prev_cp ? stbtt_GetCodepointKernAdvance(&s_sub_font, prev_cp, (int)cp) : 0;
        int glyph_x = pen_x + OVL_IROUNDF((float)kern * scale);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&s_sub_font, (int)cp, scale, scale, &x0, &y0, &x1, &y1);

        int trial_min_x = (pen_x == 0) ? (glyph_x + x0) : line_min[line];
        int trial_max_x = (pen_x == 0) ? (glyph_x + x1) : line_max[line];
        if (glyph_x + x0 < trial_min_x) {
            trial_min_x = glyph_x + x0;
        }
        if (glyph_x + x1 > trial_max_x) {
            trial_max_x = glyph_x + x1;
        }

        if (pen_x > 0 && (trial_max_x - trial_min_x) > max_w) {
            line++;
            if (line >= OVL_SUB_MAX_WRAP_LINES) {
                break;
            }
            pen_x = 0;
            prev_cp = 0;
            if (cp == ' ') {
                continue;
            }

            kern = 0;
            glyph_x = 0;
            trial_min_x = glyph_x + x0;
            trial_max_x = glyph_x + x1;
        }

        glyphs[count].cp = cp;
        glyphs[count].x = glyph_x;
        glyphs[count].y = line * line_step;
        glyphs[count].line = line;
        glyphs[count].x0 = x0;
        glyphs[count].y0 = y0;
        glyphs[count].x1 = x1;
        glyphs[count].y1 = y1;
        count++;

        line_min[line] = trial_min_x;
        line_max[line] = trial_max_x;

        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&s_sub_font, (int)cp, &advance, &lsb);
        (void)lsb;
        pen_x = glyph_x + OVL_IROUNDF((float)advance * scale);
        prev_cp = (int)cp;
    }

    if (count <= 0) {
        return 0;
    }

    int total_lines = glyphs[count - 1].line + 1;
    int first_line = total_lines > OVL_SUB_MAX_LINES ? (total_lines - OVL_SUB_MAX_LINES) : 0;
    int kept_lines = total_lines - first_line;
    int out_max_w = 0;
    int kept = 0;

    for (int ln = first_line; ln < total_lines; ln++) {
        int w = line_max[ln] - line_min[ln];
        if (w > out_max_w) {
            out_max_w = w;
        }
    }

    for (int i = 0; i < count; i++) {
        if (glyphs[i].line < first_line) {
            continue;
        }
        int ln = glyphs[i].line;
        glyphs[kept] = glyphs[i];
        glyphs[kept].x = glyphs[i].x - line_min[ln];
        glyphs[kept].y = (ln - first_line) * line_step;
        glyphs[kept].line = ln - first_line;
        kept++;
    }

    *glyph_count = kept;
    *out_w = out_max_w;
    *out_h = kept_lines * line_h + (kept_lines - 1) * OVL_SUB_LINE_GAP;
    *out_baseline = baseline;
    return (kept > 0 && *out_w > 0 && *out_h > 0);
}

int __rebuild_subtitle_texture(int max_w)
{
    if (s_subtitle[0] == '\0') {
        s_sub_draw_w = 0;
        s_sub_draw_h = 0;
        s_sub_last_max_w = max_w;
        s_sub_dirty = 0;
        return 0;
    }

    ovl_glyph_layout_t glyphs[OVL_SUB_MAX_GLYPHS];
    int glyph_count = 0;
    int tex_w = 0;
    int tex_h = 0;
    int baseline = 0;
    if (!__subtitle_build_layout(max_w, glyphs, &glyph_count, &tex_w, &tex_h, &baseline)) {
        s_sub_draw_w = 0;
        s_sub_draw_h = 0;
        s_sub_last_max_w = max_w;
        s_sub_dirty = 0;
        return 0;
    }

    size_t need = (size_t)tex_w * (size_t)tex_h;
    if (!__ensure_sub_pixel_capacity(need)) {
        return 0;
    }
    memset(s_sub_pixels, 0, need);

    float scale = stbtt_ScaleForPixelHeight(&s_sub_font, OVL_SUB_FONT_PX);
    for (int i = 0; i < glyph_count; i++) {
        int gw = glyphs[i].x1 - glyphs[i].x0;
        int gh = glyphs[i].y1 - glyphs[i].y0;
        if (gw <= 0 || gh <= 0) {
            continue;
        }
        int dst_x = glyphs[i].x + glyphs[i].x0;
        int dst_y = glyphs[i].y + baseline + glyphs[i].y0;
        if (dst_x < 0 || dst_y < 0 || dst_x + gw > tex_w || dst_y + gh > tex_h) {
            continue;
        }
        unsigned char *dst = s_sub_pixels + (size_t)dst_y * (size_t)tex_w + (size_t)dst_x;
        stbtt_MakeCodepointBitmap(&s_sub_font, dst, gw, gh, tex_w, scale, scale, (int)glyphs[i].cp);
    }

    if (!s_sub_tex) {
        glGenTextures(1, &s_sub_tex);
    }
    glBindTexture(GL_TEXTURE_2D, s_sub_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tex_w, tex_h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, s_sub_pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    s_sub_draw_w = tex_w;
    s_sub_draw_h = tex_h;
    s_sub_last_max_w = max_w;
    s_sub_dirty = 0;
    return 1;
}

int __subtitle_ascii_fallback(char *out, size_t out_sz, const char *in)
{
    size_t j = 0;
    const char *p = in;
    while (*p && j + 1 < out_sz) {
        unsigned int cp = __utf8_next(&p);
        if (cp == '\r' || cp == '\n' || cp == '\t') {
            out[j++] = ' ';
        } else if (cp >= 32 && cp <= 126) {
            out[j++] = (char)cp;
        } else {
            out[j++] = '?';
        }
    }
    out[j] = '\0';
    return (int)j;
}

/* ---------------------------------------------------------------------------
 * Helpers: push geometry
 * --------------------------------------------------------------------------- */

