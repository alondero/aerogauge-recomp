# Dependency-free tests shared by the full build and AERO_HOST_TESTS_ONLY.
add_executable(test_gyro tests/test_gyro.cpp)
target_include_directories(test_gyro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME gyro_steering COMMAND test_gyro)

add_executable(test_hud_shift_scale tests/test_hud_shift_scale.c)
target_include_directories(test_hud_shift_scale PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT WIN32)
    target_link_libraries(test_hud_shift_scale PRIVATE m)
endif()
add_test(NAME hud_shift_scale COMMAND test_hud_shift_scale)

add_executable(test_full_track_policy tests/test_full_track_policy.cpp)
target_include_directories(test_full_track_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME full_track_policy COMMAND test_full_track_policy)

add_executable(test_startup_mode tests/test_startup_mode.cpp src/aero_startup.cpp)
target_include_directories(test_startup_mode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME startup_mode COMMAND test_startup_mode)

add_executable(test_rom_startup tests/test_rom_startup.cpp)
target_include_directories(test_rom_startup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME rom_startup COMMAND test_rom_startup)

add_executable(test_input_cadence tests/test_input_cadence.cpp)
target_include_directories(test_input_cadence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME input_cadence COMMAND test_input_cadence)

add_executable(test_user_data_dir tests/test_user_data_dir.cpp)
target_include_directories(test_user_data_dir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME user_data_dir COMMAND test_user_data_dir)

# The developer warp chord: both modifiers are required, one press selects one
# track, and releasing the chord re-arms it. No SDL or ROM needed.
add_executable(test_dev_warp_chord tests/test_dev_warp_chord.cpp)
target_include_directories(test_dev_warp_chord PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME dev_warp_chord COMMAND test_dev_warp_chord)
