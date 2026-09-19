#include "../include/desktop_platform_sdl.h"
#include "../include/engine_sim_application.h"
#include "../include/sdl_audio_output.h"
#include "../include/sdl_gpu_renderer.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>

int main(int, char **) {
    DesktopPlatformSdl platform;
    if (!platform.initialize("Open Engine Simulator", 1280, 720)) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", platform.lastError().c_str());
        return 1;
    }

    const char *internalStorage = SDL_GetAndroidInternalStoragePath();
    if (internalStorage == nullptr) {
        std::fprintf(stderr, "Android storage path unavailable: %s\n", SDL_GetError());
        platform.shutdown();
        return 1;
    }

    RuntimePaths paths;
    paths.applicationDirectory = internalStorage;
    paths.assetDirectory = std::filesystem::path(internalStorage) / "assets";

    const std::filesystem::path shaderDirectory = paths.assetDirectory / "shaders";
    SdlGpuRenderer renderer;
    if (!renderer.initialize(platform.nativeWindowHandle(), shaderDirectory.string())) {
        std::fprintf(stderr, "SDL GPU initialization failed: %s\n", renderer.lastError());
        platform.shutdown();
        return 1;
    }

    EngineSimApplication application;
    SdlAudioOutput audioOutput;
    application.initialize(&platform, &renderer, &audioOutput, paths);
    application.run();
    application.destroy();

    renderer.shutdown();
    platform.shutdown();
    return 0;
}
