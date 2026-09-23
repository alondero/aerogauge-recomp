# RecompFrontend's shader helpers consume DXC variables in the caller's scope.
set(AERO_DXC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc")
if(ANDROID)
    set(DXC ${AERO_HOST_DXC})
elseif(WIN32)
    set(DXC "${AERO_DXC_ROOT}/bin/x64/dxc.exe")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(DXC "DYLD_LIBRARY_PATH=${AERO_DXC_ROOT}/lib/x64" "${AERO_DXC_ROOT}/bin/x64/dxc-macos")
    else()
        set(DXC "DYLD_LIBRARY_PATH=${AERO_DXC_ROOT}/lib/arm64" "${AERO_DXC_ROOT}/bin/arm64/dxc-macos")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(DXC "LD_LIBRARY_PATH=${AERO_DXC_ROOT}/lib/x64" "${AERO_DXC_ROOT}/bin/x64/dxc-linux")
else()
    set(DXC "LD_LIBRARY_PATH=${AERO_DXC_ROOT}/lib/arm64" "${AERO_DXC_ROOT}/bin/arm64/dxc-linux")
endif()
set(DXC_COMMON_OPTS "-I${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")
set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_3" "-fvk-invert-y")
set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH "${N64MR}" CACHE PATH "" FORCE)
set(RECOMP_FRONTEND_RT64_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64" CACHE PATH "" FORCE)
if(ANDROID)
    set(sdl2_SOURCE_DIR "${AERO_ANDROID_DEPS}/SDL")
else()
    set(sdl2_SOURCE_DIR "${SDL2_WIN32_DEPS}")
endif()
add_subdirectory(lib/RecompFrontend)

# These static libraries call into each other, including when the host omits
# the controls tab. Declare the cycle so CMake repeats the archives for
# linkers that resolve static libraries from left to right.
target_link_libraries(recompui PUBLIC recompinput rt64)
target_link_libraries(recompinput PUBLIC recompui)
foreach(frontend_target recompui recompinput rmlui_core rmlui_debugger)
    if(NOT MSVC)
        target_compile_options(${frontend_target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include>" "$<$<COMPILE_LANGUAGE:CXX>:cstdint>")
    endif()
    target_include_directories(${frontend_target} PRIVATE ${SDL2_INCLUDE_DIRS})
endforeach()
target_sources(aerogauge_modern PRIVATE src/ui/aero_frontend_settings.cpp)
target_include_directories(aerogauge_modern PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(aerogauge_modern PRIVATE recompui recompinput)

# This host-only test exercises the settings adapter without a ROM or generated
# game output.
if(NOT ANDROID)
add_executable(test_frontend_settings tests/test_frontend_settings.cpp
    src/ui/aero_frontend_settings.cpp src/aero_config.cpp)
target_include_directories(test_frontend_settings PRIVATE src ${SDL2_INCLUDE_DIRS})
# aero_config starts a std::thread for the debounced persistence worker
# (Threads::Threads is resolved once in the top-level CMakeLists.txt).
target_link_libraries(test_frontend_settings PRIVATE recompui recompinput librecomp ultramodern rt64 Threads::Threads)
if(WIN32)
    target_compile_definitions(test_frontend_settings PRIVATE SDL_MAIN_HANDLED NOMINMAX)
    target_link_libraries(test_frontend_settings PRIVATE SDL2 shell32)
    # This test links RT64 and recompui, so it needs the same runtime DLLs the
    # game target copies. Without them CTest exits 0xC0000135 (DLL not found)
    # unless those directories happen to be on PATH.
    add_custom_command(TARGET test_frontend_settings POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SDL2_WIN32_DEPS}/lib/x64/SDL2.dll
            ${AERO_DXC_ROOT}/bin/x64/dxcompiler.dll
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/freetype-windows-binaries/release dll/win64/freetype.dll"
            $<TARGET_FILE_DIR:test_frontend_settings>)
else()
    target_link_libraries(test_frontend_settings PRIVATE ${SDL2_LIBRARIES})
endif()
add_test(NAME frontend_settings COMMAND test_frontend_settings)
endif()

# Release and source-build output needs the frontend assets and RmlUi fonts
# beside the executable. Desktop builds also stage the window icon here.
# Windows needs the FreeType runtime DLL below.
set(AERO_RUNTIME_ASSETS
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LatoLatin-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LatoLatin-Bold.ttf)
if(NOT ANDROID)
    list(APPEND AERO_RUNTIME_ASSETS
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/aerogauge-icon.bmp
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/aerogauge-icon.png)
endif()
add_custom_command(TARGET aerogauge_modern POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/frontend $<TARGET_FILE_DIR:aerogauge_modern>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${AERO_RUNTIME_ASSETS}
        $<TARGET_FILE_DIR:aerogauge_modern>/assets)
if(WIN32)
    add_custom_command(TARGET aerogauge_modern POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/freetype-windows-binaries/release dll/win64/freetype.dll"
            $<TARGET_FILE_DIR:aerogauge_modern>)
endif()
