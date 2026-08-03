# platform/media_hal — na51090 hdal 封装（videodec/videoproc/videoout）
# ⚠️ 仅目标机(aarch64)交叉编译：需 BSP 的 hdal 头与库。
cmake_minimum_required(VERSION 3.13)
project(mhal C)

# BSP 根：na51090 SDK（含 hdal 头/库，约 1.6G，不随本工程分发）。
# 必须由构建者提供：-DBSP_ROOT=/path/to/na51090_linux_sdk
# 环境变量 BSP_ROOT 亦可；或写入 platform/bsp_ref.txt 首行路径。
if(NOT DEFINED BSP_ROOT)
    if(DEFINED ENV{BSP_ROOT})
        set(BSP_ROOT $ENV{BSP_ROOT})
    elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bsp_ref.txt)
        file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/bsp_ref.txt _bsp_lines
             REGEX "^[^#].*na51090" LIMIT_COUNT 1)
        list(GET _bsp_lines 0 BSP_ROOT)
        string(STRIP "${BSP_ROOT}" BSP_ROOT)
    endif()
endif()
if(NOT BSP_ROOT OR NOT EXISTS ${BSP_ROOT}/code/hdal)
    message(FATAL_ERROR
        "media_hal 需要 na51090 BSP。请传 -DBSP_ROOT=/path/to/na51090_linux_sdk "
        "(当前: '${BSP_ROOT}')。仅目标机(NVR_WITH_ONBOARD=ON)构建 media_hal/app 时需要。")
endif()
set(HDAL ${BSP_ROOT}/code/hdal)

add_library(mhal STATIC
    mhal_vout.c
    mhal_vdec.c
    mhal_budget.c
)

target_include_directories(mhal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}          # mhal_vdec.h / mhal_vout.h / mhal_budget.h
)
target_include_directories(mhal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include)  # nvr_log.h
target_include_directories(mhal PRIVATE
    ${HDAL}/include                      # hdal.h + hd_*.h
    ${HDAL}/vendor/isp/include           # vendor_* (如需 vendor_videodec 精调)
)

# 链接 hdal 预编译库（BSP，aarch64）+ pthread；PUBLIC 传递给最终 exe
if(EXISTS ${HDAL}/output/libhdal.so)
    target_link_libraries(mhal PUBLIC ${HDAL}/output/libhdal.so pthread)
elseif(EXISTS ${HDAL}/source/libhdal.so)
    target_link_libraries(mhal PUBLIC ${HDAL}/source/libhdal.so pthread)
else()
    message(WARNING "libhdal.so 未找到于 ${HDAL}/output|source；最终链接会缺 hd_* 符号")
endif()

set_target_properties(mhal PROPERTIES C_STANDARD 11)

# 提示：本机(x86)只能 -fsyntax-only 校验（已通过）；出库需 aarch64 工具链 + hdal.so。
