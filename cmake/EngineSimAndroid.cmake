include(cmake/EngineSimSDL.cmake)
engine_sim_require_sdl3()

# SDL's Android Activity loads the application entry point from libmain.so.
# Keep the platform adapter, audio adapter, and GPU renderer shared with the
# desktop host so Android exercises the same simulator path.
add_library(main SHARED
    src/android_main.cpp
    src/desktop_platform_sdl.cpp
    src/sdl_audio_util.cpp
    src/sdl_audio_output.cpp
    src/sdl_gpu_renderer.cpp)

target_link_libraries(main PRIVATE SDL3::SDL3 engine-sim-visualization log)
target_compile_features(main PRIVATE cxx_std_17)
set_target_properties(main PROPERTIES OUTPUT_NAME "main")

# All engine libraries are linked into the Android shared object.
set(_engine_sim_android_pic_targets
    simple-2d-constraint-solver
    engine-sim-core
    engine-sim-render-support
    engine-sim-visualization)
if(TARGET piranha)
    list(APPEND _engine_sim_android_pic_targets piranha)
endif()
if(TARGET engine-sim-scripting)
    list(APPEND _engine_sim_android_pic_targets engine-sim-scripting)
endif()
set_property(TARGET ${_engine_sim_android_pic_targets}
    PROPERTY POSITION_INDEPENDENT_CODE ON)
