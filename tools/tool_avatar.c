/**
 * @file tool_avatar.c
 * @brief MCP tools for the 3D VRM avatar (animations, emotions, blend shapes)
 * @version 0.1
 * @date 2026-06-30
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "tool_avatar.h"

#include "tal_api.h"
#include "ai_mcp_server.h"
#include "tuya_kconfig.h"

#if defined(VRM_MODEL_PATH)

#include "vrm_renderer.h"
#include "cJSON.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Tool handlers
 * --------------------------------------------------------------------------- */

/**
 * @brief Play a named VRMA body animation
 * @param[in] properties MCP input properties (expects "name")
 * @param[out] ret_val Boolean result
 * @param[in] user_data Unused
 * @return OPRT_OK
 */
static OPERATE_RET __play_animation(const MCP_PROPERTY_LIST_T *properties,
                                    MCP_RETURN_VALUE_T *ret_val,
                                    void *user_data)
{
    (void)user_data;
    const char *name = NULL;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "name") == 0 &&
            prop->type == MCP_PROPERTY_TYPE_STRING) {
            name = prop->default_val.str_val;
            break;
        }
    }

    if (!name || name[0] == '\0') {
        ai_mcp_return_value_set_bool(ret_val, FALSE);
        return OPRT_OK;
    }

    vrm_viewer_play_animation(name);
    ai_mcp_return_value_set_bool(ret_val, TRUE);
    return OPRT_OK;
}

/**
 * @brief Set the avatar's facial emotion preset
 * @param[in] properties MCP input (expects "emotion", optional "intensity")
 * @param[out] ret_val Boolean result
 * @param[in] user_data Unused
 * @return OPRT_OK
 */
static OPERATE_RET __set_emotion(const MCP_PROPERTY_LIST_T *properties,
                                 MCP_RETURN_VALUE_T *ret_val,
                                 void *user_data)
{
    (void)user_data;
    const char *emotion = NULL;
    int intensity_pct = 100;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "emotion") == 0 &&
            prop->type == MCP_PROPERTY_TYPE_STRING) {
            emotion = prop->default_val.str_val;
        } else if (strcmp(prop->name, "intensity") == 0 &&
                   prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            intensity_pct = prop->default_val.int_val;
        }
    }

    if (!emotion || emotion[0] == '\0') {
        ai_mcp_return_value_set_bool(ret_val, FALSE);
        return OPRT_OK;
    }

    float intensity = (float)intensity_pct / 100.0f;
    if (intensity < 0.0f) {
        intensity = 0.0f;
    }
    if (intensity > 1.0f) {
        intensity = 1.0f;
    }

    vrm_viewer_set_emotion(emotion, intensity, 0.0f);
    ai_mcp_return_value_set_bool(ret_val, TRUE);
    return OPRT_OK;
}

/**
 * @brief Set individual VRM expression weights via JSON
 * @param[in] properties MCP input (expects "expressions" JSON string)
 * @param[out] ret_val Boolean result
 * @param[in] user_data Unused
 * @return OPRT_OK
 */
static OPERATE_RET __set_blendshape(const MCP_PROPERTY_LIST_T *properties,
                                    MCP_RETURN_VALUE_T *ret_val,
                                    void *user_data)
{
    (void)user_data;
    const char *json_str = NULL;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "expressions") == 0 &&
            prop->type == MCP_PROPERTY_TYPE_STRING) {
            json_str = prop->default_val.str_val;
            break;
        }
    }

    if (!json_str || json_str[0] == '\0') {
        ai_mcp_return_value_set_bool(ret_val, FALSE);
        return OPRT_OK;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ai_mcp_return_value_set_bool(ret_val, FALSE);
        return OPRT_OK;
    }

    vrm_viewer_clear_blendshapes();

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (item->string && cJSON_IsNumber(item)) {
            float w = (float)item->valuedouble;
            if (w < 0.0f) {
                w = 0.0f;
            }
            if (w > 1.0f) {
                w = 1.0f;
            }
            vrm_viewer_set_blendshape(item->string, w);
        }
    }

    cJSON_Delete(root);
    ai_mcp_return_value_set_bool(ret_val, TRUE);
    return OPRT_OK;
}

/**
 * @brief Trigger body animation and facial emotion in one call
 * @param[in] properties MCP input (expects "animation", "emotion", optional "intensity")
 * @param[out] ret_val Boolean result
 * @param[in] user_data Unused
 * @return OPRT_OK
 */
static OPERATE_RET __composite_action(const MCP_PROPERTY_LIST_T *properties,
                                      MCP_RETURN_VALUE_T *ret_val,
                                      void *user_data)
{
    (void)user_data;
    const char *animation = NULL;
    const char *emotion = NULL;
    int intensity_pct = 100;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "animation") == 0 &&
            prop->type == MCP_PROPERTY_TYPE_STRING) {
            animation = prop->default_val.str_val;
        } else if (strcmp(prop->name, "emotion") == 0 &&
                   prop->type == MCP_PROPERTY_TYPE_STRING) {
            emotion = prop->default_val.str_val;
        } else if (strcmp(prop->name, "intensity") == 0 &&
                   prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            intensity_pct = prop->default_val.int_val;
        }
    }

    if ((!animation || animation[0] == '\0') &&
        (!emotion || emotion[0] == '\0')) {
        ai_mcp_return_value_set_bool(ret_val, FALSE);
        return OPRT_OK;
    }

    if (animation && animation[0] != '\0') {
        vrm_viewer_play_animation(animation);
    }

    if (emotion && emotion[0] != '\0') {
        float intensity = (float)intensity_pct / 100.0f;
        if (intensity < 0.0f) {
            intensity = 0.0f;
        }
        if (intensity > 1.0f) {
            intensity = 1.0f;
        }
        vrm_viewer_set_emotion(emotion, intensity, 0.0f);
    }

    ai_mcp_return_value_set_bool(ret_val, TRUE);
    return OPRT_OK;
}

/**
 * @brief Register VRM avatar MCP tools
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tool_avatar_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "avatar_play_animation",
        "Play a one-shot body animation. Auto-returns to idle when done.\n"
        "Animations: idle_normal, idle_boring, happy_idle, say_hello, "
        "standing_greeting, wave, excited, joy, thinking, look_around, "
        "show, crying, squat, shoot, bier.",
        __play_animation,
        NULL,
        MCP_PROP_STR("name", "Animation name (exact match).")
    ));

    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "avatar_set_emotion",
        "Set the avatar's facial expression. Transitions smoothly.\n"
        "Emotions: neutral, happy, sad, angry, surprised, wink, thinking, "
        "cool, relaxed, embarrassed, confident, sleep, silly, confused, "
        "loving, laughing, shocked, fearful, kissy, delicious.",
        __set_emotion,
        NULL,
        MCP_PROP_STR("emotion", "Emotion name."),
        MCP_PROP_INT_DEF_RANGE("intensity",
                               "Strength 0-100 (default 100).",
                               100, 0, 100)
    ));

    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "avatar_set_blendshape",
        "Set fine-grained VRM expression weights via JSON object "
        "(keys = blend shape names, values = 0.0-1.0). Replaces current weights.",
        __set_blendshape,
        NULL,
        MCP_PROP_STR("expressions",
                     "JSON object, e.g. {\"happy\":0.8,\"blink\":0.2}.")
    ));

    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "avatar_composite_action",
        "Set body animation AND facial emotion together in one call.\n"
        "Prefer this over separate play_animation + set_emotion calls.",
        __composite_action,
        NULL,
        MCP_PROP_STR("animation", "Body animation name."),
        MCP_PROP_STR("emotion", "Facial emotion name."),
        MCP_PROP_INT_DEF_RANGE("intensity",
                               "Strength 0-100 (default 100).",
                               100, 0, 100)
    ));

    PR_DEBUG("VRM avatar MCP tools registered");
    return rt;
}

#else /* !VRM_MODEL_PATH */

/**
 * @brief Register VRM avatar MCP tools (disabled)
 * @return OPRT_OK
 */
OPERATE_RET tool_avatar_register(void)
{
    return OPRT_OK;
}

#endif /* VRM_MODEL_PATH */
