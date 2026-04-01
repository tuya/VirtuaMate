/**
 * @file tools_register.c
 * @brief MCP tools registration for DuckyClaw
 * @version 0.1
 * @date 2025-03-25
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tools_register.h"
#include "tool_files.h"
#include "tool_cron.h"
#include "tool_openclaw_ctrl.h"
#include "cron_service.h"
#include "heartbeat.h"
#include "memory_manager.h"
#include "session_manager.h"
#include "skill_loader.h"

#include "tal_api.h"

#if defined(PLATFORM_LINUX) && (PLATFORM_LINUX == 1)
#include "tool_exec.h"
#endif

/***********************************************************
***********************function define**********************
***********************************************************/

static OPERATE_RET __ai_mcp_init(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(ai_mcp_server_init("DuckyClaw MCP Server", "1.0"));

    TUYA_CALL_ERR_RETURN(tool_files_fs_init());

    TUYA_CALL_ERR_LOG(cron_service_init());
    TUYA_CALL_ERR_LOG(cron_service_start());

    TUYA_CALL_ERR_LOG(heartbeat_init());
    TUYA_CALL_ERR_LOG(heartbeat_start());

    TUYA_CALL_ERR_LOG(memory_manager_init());
    TUYA_CALL_ERR_LOG(session_manager_init());

    TUYA_CALL_ERR_LOG(skill_loader_init());

    TUYA_CALL_ERR_RETURN(tool_files_register());
    TUYA_CALL_ERR_RETURN(tool_cron_register());

    #if defined(PLATFORM_LINUX) && (PLATFORM_LINUX == 1)
    TUYA_CALL_ERR_RETURN(tool_exec_register());
    #endif

    TUYA_CALL_ERR_RETURN(tool_openclaw_ctrl_register());

    PR_NOTICE("DuckyClaw MCP tools all registered");
    return rt;
}

/**
 * @brief Re-register the MCP callback on every MQTT reconnect.
 *
 * The AI agent re-initialises on each MQTT connected event, which clears
 * the MCP callback set during ai_mcp_server_init.  This NORMAL (persistent)
 * handler ensures the callback is restored after every agent re-init.
 */
static OPERATE_RET __ai_mcp_ensure_cb(void *data)
{
    (void)data;
    ai_mcp_server_init("DuckyClaw MCP Server", "1.0");
    return OPRT_OK;
}

/**
 * @brief Initialize and register all MCP tools
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tool_registry_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    tal_event_subscribe(EVENT_MQTT_CONNECTED, "ducky_claw_mcp_init", __ai_mcp_init, SUBSCRIBE_TYPE_ONETIME);
    tal_event_subscribe(EVENT_MQTT_CONNECTED, "ducky_claw_mcp_cb", __ai_mcp_ensure_cb, SUBSCRIBE_TYPE_NORMAL);

    PR_DEBUG("DuckyClaw MCP tools registered successfully");

    return rt;
}
