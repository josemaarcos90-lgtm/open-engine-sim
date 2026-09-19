#include "../include/desktop_platform_sdl.h"
#include "../include/engine_sim_application.h"
#include "../include/sdl_audio_output.h"
#include "../include/sdl_gpu_renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/log.h>
#include <filesystem>

namespace {
constexpr const char *LogTag = "OpenEngineSim";

void logInfo(const char *message) {
    __android_log_print(ANDROID_LOG_INFO, LogTag, "%s", message);
}

void logError(const char *stage, const char *error) {
    __android_log_print(ANDROID_LOG_ERROR, LogTag, "%s: %s", stage, error != nullptr ? error : "(no error)");
}
}

int main(int, char **) {
    logInfo("Android native entry point reached");

    DesktopPlatformSdl platform;
    logInfo("Initializing SDL video/audio");
    if (!platform.initialize("Open Engine Simulator", 1280, 720)) {
        logError("SDL initialization failed", platform.lastError().c_str());
        return 1;
    }
    logInfo("SDL window created");

    const char *internalStorage = SDL_GetAndroidInternalStoragePath();
    if (internalStorage == nullptr) {
        logError("Android storage path unavailable", SDL_GetError());
        platform.shutdown();
        return 1;
    }
    __android_log_print(ANDROID_LOG_INFO, LogTag, "Internal storage: %s", internalStorage);

    RuntimePaths paths;
    paths.applicationDirectory = internalStorage;
    paths.assetDirectory = std::filesystem::path(internalStorage) / "assets";

    const std::filesystem::path shaderDirectory = paths.assetDirectory / "shaders";
    const std::filesystem::path vertexShader = shaderDirectory / "engine_sim.vertex.spv";
    const std::filesystem::path fragmentShader = shaderDirectory / "engine_sim.fragment.spv";
    __android_log_print(ANDROID_LOG_INFO, LogTag, "Assets: %s", paths.assetDirectory.string().c_str());
    __android_log_print(ANDROID_LOG_INFO, LogTag, "SPIR-V vertex exists=%d fragment exists=%d",
        std::filesystem::exists(vertexShader) ? 1 : 0,
        std::filesystem::exists(fragmentShader) ? 1 : 0);

    SdlGpuRenderer renderer;
    logInfo("Initializing SDL GPU renderer");
    if (!renderer.initialize(platform.nativeWindowHandle(), shaderDirectory.string())) {
        logError("SDL GPU initialization failed", renderer.lastError());
        platform.shutdown();
        return 1;
    }
    logInfo("SDL GPU renderer initialized");

    EngineSimApplication application;
    SdlAudioOutput audioOutput;
    logInfo("Initializing EngineSimApplication");
    application.initialize(&platform, &renderer, &audioOutput, paths);
    logInfo("EngineSimApplication initialized; entering run loop");
    application.run();
    logInfo("EngineSimApplication run loop exited");
    application.destroy();

    renderer.shutdown();
    platform.shutdown();
    logInfo("Clean shutdown");
    return 0;
}
