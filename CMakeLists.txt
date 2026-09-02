cmake_minimum_required(VERSION 3.25)
project(VedantuClickerSystem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Windows-specific definitions ──────────────────────────────────────────────
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
add_definitions(-D_WIN32_WINNT=0x0A00)  # Windows 10+

# ── MSVC compiler flags ───────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# ── Dependencies (via vcpkg) ──────────────────────────────────────────────────
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog        CONFIG REQUIRED)
find_package(OpenSSL              REQUIRED)
find_package(CURL                 REQUIRED)
find_package(ixwebsocket   CONFIG REQUIRED)
find_package(imgui         CONFIG REQUIRED)

# ── Source files ──────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.hpp"
)

add_executable(VedantuClickerSystem WIN32 ${SOURCES} resource.rc)

# Allow the resource compiler to resolve src/ headers (e.g. hid/resource.h)
set_property(TARGET VedantuClickerSystem
    PROPERTY RESOURCE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(VedantuClickerSystem PROPERTIES
    OUTPUT_NAME              "VedantuClickerSystem"
    MSVC_RUNTIME_LIBRARY     "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_include_directories(VedantuClickerSystem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(VedantuClickerSystem PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    ixwebsocket::ixwebsocket
    imgui::imgui
    # Windows system libraries
    ws2_32
    crypt32
    bcrypt
    d3d11
    dxgi
    d3dcompiler
    Dwmapi
    Shcore
    Advapi32
)

# =============================================================================
#  SDK embed — copy SunVote SDK binaries into sdk_embed/ so resource.rc
#  can embed them as RCDATA.
#
#  This only runs when SUNVOTE_SDK_DIR is set, which is required only on the
#  first CMake configure by the developer who has access to the SDK installer.
#  Pre-copied binaries committed to sdk_embed/ are used by all other developers.
#
#  Usage:
#    cmake -B build -DSUNVOTE_SDK_DIR="C:/path/to/SunVote WSapp for win-5.1.0.43" ...
# =============================================================================
if(SUNVOTE_SDK_DIR)
    message(STATUS "Copying SunVote SDK binaries from: ${SUNVOTE_SDK_DIR}")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/sdk_embed")
    foreach(SDK_FILE
        QTWSCmdApp.exe QTZT_SDK_V4.dll Qt5Core.dll Qt5Network.dll
        Qt5WebSockets.dll hidapi.dll libcurl.dll log4cplusU.dll
        quazip1-qt5.dll zlib.dll SDK_V4_resources.rcc config.ini log.properties)
        file(COPY "${SUNVOTE_SDK_DIR}/${SDK_FILE}"
             DESTINATION "${CMAKE_SOURCE_DIR}/sdk_embed")
    endforeach()

    # Runtime redistributables from Windows SysWOW64
    foreach(REDIST_DLL
        MSVCP140.dll MSVCP140_1.dll MSVCP140_2.dll VCRUNTIME140.dll
        vccorlib140.dll concrt140.dll msvcp140_atomic_wait.dll
        msvcp140_codecvt_ids.dll)
        file(COPY "$ENV{SystemRoot}/SysWOW64/${REDIST_DLL}"
             DESTINATION "${CMAKE_SOURCE_DIR}/sdk_embed")
    endforeach()
endif()

# ── Post-build: stage the release executable into AppRelease/ ─────────────────
set(APP_RELEASE_DIR "${CMAKE_SOURCE_DIR}/AppRelease")

add_custom_command(TARGET VedantuClickerSystem POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RELEASE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:VedantuClickerSystem>"
        "${APP_RELEASE_DIR}/VedantuClickerSystem.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/.env.example"
        "${APP_RELEASE_DIR}/.env.example"
    COMMENT "Staging release artefacts to AppRelease/..."
)

install(TARGETS VedantuClickerSystem DESTINATION bin)
