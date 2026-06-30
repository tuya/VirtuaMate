/**
 * @file tool_avatar.h
 * @brief MCP tools for 3D VRM avatar control
 * @version 0.1
 * @date 2026-06-30
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef __TOOL_AVATAR_H__
#define __TOOL_AVATAR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Register VRM avatar MCP tools
 * @return OPRT_OK on success, error code on failure
 * @note No-op when VRM_MODEL_PATH is not configured at build time.
 */
OPERATE_RET tool_avatar_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __TOOL_AVATAR_H__ */
