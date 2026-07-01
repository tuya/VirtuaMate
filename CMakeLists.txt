##
# @file CMakeLists.txt
# @brief 
#/

# APP_PATH
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})

# APP_NAME
get_filename_component(APP_NAME ${APP_PATH} NAME)

# APP_SRCS
aux_source_directory(${APP_PATH}/src APP_SRCS)
aux_source_directory(${APP_PATH}/tools APP_SRCS)
aux_source_directory(${APP_PATH}/cron_service APP_SRCS)
aux_source_directory(${APP_PATH}/heartbeat APP_SRCS)
aux_source_directory(${APP_PATH}/memory APP_SRCS)
aux_source_directory(${APP_PATH}/gateway APP_SRCS)
aux_source_directory(${APP_PATH}/skills APP_SRCS)
aux_source_directory(${APP_PATH}/agent APP_SRCS)

# VRM 3D renderer sources
aux_source_directory(${APP_PATH}/src/vrm APP_VRM_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/loader APP_VRM_LOADER_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/renderer APP_VRM_RENDERER_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/overlay APP_VRM_OVERLAY_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/emotion APP_VRM_EMOTION_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/spring_bone APP_VRM_SPRING_SRCS)
aux_source_directory(${APP_PATH}/src/vrm/scene APP_VRM_SCENE_SRCS)
list(APPEND APP_SRCS
    ${APP_VRM_SRCS}
    ${APP_VRM_LOADER_SRCS}
    ${APP_VRM_RENDERER_SRCS}
    ${APP_VRM_OVERLAY_SRCS}
    ${APP_VRM_EMOTION_SRCS}
    ${APP_VRM_SPRING_SRCS}
    ${APP_VRM_SCENE_SRCS}
)

# APP_INC
set(APP_INC
    ${APP_PATH}
    ${APP_PATH}/include
    ${APP_PATH}/tools
    ${APP_PATH}/cron_service
    ${APP_PATH}/heartbeat
    ${APP_PATH}/memory
    ${APP_PATH}/gateway
    ${APP_PATH}/skills
    ${APP_PATH}/agent
    ${APP_PATH}/TuyaOpen/src/ai_components/assets/include
    ${APP_PATH}/third_party/stb
    ${APP_PATH}/src/vrm
    ${APP_PATH}/TuyaOpen/src/liblvgl/v9/lvgl/src/libs/tiny_ttf
)

include(${APP_PATH}/cmake/vrm_linux_deps.cmake)

########################################
# Target Configure
########################################
add_library(${EXAMPLE_LIB})

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRCS}
    )

# Snapshot before subdirectories so we can identify what the app itself adds.
set(_APP_COMP_LIBS_BEFORE "${COMPONENT_LIBS}")
set(_APP_COMP_PUBINC_BEFORE "${COMPONENT_PUBINC}")

add_subdirectory(${APP_PATH}/IM ${CMAKE_BINARY_DIR}/IM)
add_subdirectory(${APP_PATH}/components/lua ${CMAKE_BINARY_DIR}/components/lua)

# Propagate new component libs/includes to EXAMPLE_LIB.
set(_APP_NEW_LIBS "${COMPONENT_LIBS}")
list(REMOVE_ITEM _APP_NEW_LIBS ${_APP_COMP_LIBS_BEFORE})
set(_APP_NEW_PUBINC "${COMPONENT_PUBINC}")
list(REMOVE_ITEM _APP_NEW_PUBINC ${_APP_COMP_PUBINC_BEFORE})

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_INC}
        ${_APP_NEW_PUBINC}
    )

if(VRM_RENDER_COMPILE_FLAGS)
    target_compile_options(${EXAMPLE_LIB} PRIVATE ${VRM_RENDER_COMPILE_FLAGS})
endif()

if(_APP_NEW_LIBS)
    set(_APP_FULL_HDIR ${HEADER_DIR} ${_APP_NEW_PUBINC})
    deal_with_components("${_APP_NEW_LIBS}" "${_APP_FULL_HDIR}")
    target_link_libraries(${EXAMPLE_LIB} ${_APP_NEW_LIBS})

    foreach(_lib ${_APP_NEW_LIBS})
        target_sources(${EXAMPLE_LIB} PRIVATE $<TARGET_OBJECTS:${_lib}>)
    endforeach()
endif()

########################################
# ai_components overlay (ai_agent + ai_mcp patches)
########################################
set(_DC_OVERLAY ${APP_PATH}/overlay/ai_components)

function(_dc_overlay_source target rel_path)
    if(NOT TARGET ${target})
        message(WARNING "[DC overlay] target '${target}' not defined; skip ${rel_path}")
        return()
    endif()
    set(_overlay_file ${_DC_OVERLAY}/${rel_path})
    if(NOT EXISTS ${_overlay_file})
        message(FATAL_ERROR "[DC overlay] missing overlay file: ${_overlay_file}")
    endif()
    get_target_property(_srcs ${target} SOURCES)
    get_filename_component(_basename ${rel_path} NAME)
    set(_new_srcs)
    set(_found_upstream FALSE)
    foreach(_s ${_srcs})
        get_filename_component(_n ${_s} NAME)
        if(_n STREQUAL "${_basename}")
            set(_found_upstream TRUE)
        else()
            list(APPEND _new_srcs ${_s})
        endif()
    endforeach()
    list(APPEND _new_srcs ${_overlay_file})
    set_target_properties(${target} PROPERTIES SOURCES "${_new_srcs}")
    if(_found_upstream)
        message(STATUS "[DC overlay] ${target} <- ${_overlay_file} (replaced upstream ${_basename})")
    else()
        message(WARNING "[DC overlay] ${target}: upstream ${_basename} not found in SOURCES; overlay appended anyway")
    endif()
endfunction()

foreach(_lib_hdir ai_mcp ai_agent ai_skills utility)
    if(TARGET ${_lib_hdir})
        target_include_directories(${_lib_hdir} BEFORE PUBLIC ${_DC_OVERLAY}/${_lib_hdir}/include)
        message(STATUS "[DC overlay] ${_lib_hdir} include path prepended: ${_DC_OVERLAY}/${_lib_hdir}/include")
    endif()
    target_include_directories(${EXAMPLE_LIB} BEFORE PRIVATE ${_DC_OVERLAY}/${_lib_hdir}/include)
    message(STATUS "[DC overlay] ${EXAMPLE_LIB} include path prepended: ${_DC_OVERLAY}/${_lib_hdir}/include")
endforeach()

_dc_overlay_source(ai_mcp     ai_mcp/src/ai_mcp_server.c)
_dc_overlay_source(ai_agent   ai_agent/src/ai_agent.c)
_dc_overlay_source(ai_skills  ai_skills/src/ai_skill.c)

if(TARGET ai_skills)
    target_include_directories(ai_skills BEFORE PUBLIC ${_DC_OVERLAY}/utility/include)
endif()
