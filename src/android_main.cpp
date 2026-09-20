#include "../include/desktop_platform_sdl.h"
#include "../include/engine_sim_application.h"
#include "../include/sdl_audio_output.h"
#include "../include/sdl_gpu_renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/log.h>
#include <jni.h>
#include <filesystem>
#include <string>

namespace {
constexpr const char *LogTag = "OpenEngineSim";

void logInfo(const char *message) { __android_log_print(ANDROID_LOG_INFO, LogTag, "%s", message); }
void logError(const char *stage, const char *error) {
    __android_log_print(ANDROID_LOG_ERROR, LogTag, "%s: %s", stage, error ? error : "(no error)");
}
void callActivityStringMethod(const char *methodName, const std::string &message) {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    if (!cls) return;
    jmethodID method = env->GetMethodID(cls, methodName, "(Ljava/lang/String;)V");
    if (method) {
        jstring text = env->NewStringUTF(message.c_str());
        env->CallVoidMethod(activity, method, text);
        env->DeleteLocalRef(text);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
}
void showStatus(const std::string &message) {
    logInfo(message.c_str());
    callActivityStringMethod("updateNativeStatus", message);
}
void hideDiagnostics() {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    if (!cls) return;
    jmethodID method = env->GetMethodID(cls, "hideNativeDiagnostics", "()V");
    if (method) env->CallVoidMethod(activity, method);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
}
}

int main(int, char **) {
    showStatus("1/6 C++ MAIN: OK\n2/6 SDL: iniciando...");
    DesktopPlatformSdl platform;
    if (!platform.initialize("Open Engine Simulator", 1280, 720)) {
        const std::string error = platform.lastError();
        logError("SDL initialization failed", error.c_str());
        showStatus("1/6 C++ MAIN: OK\n2/6 SDL: FALHOU\n" + error);
        return 1;
    }

    showStatus("1/6 C++ MAIN: OK\n2/6 SDL + JANELA: OK\n3/6 STORAGE: verificando...");
    const char *internalStorage = SDL_GetAndroidInternalStoragePath();
    if (!internalStorage) {
        const std::string error = SDL_GetError();
        showStatus("1/6 C++ MAIN: OK\n2/6 SDL + JANELA: OK\n3/6 STORAGE: FALHOU\n" + error);
        platform.shutdown();
        return 1;
    }

    RuntimePaths paths;
    paths.applicationDirectory = internalStorage;
    paths.assetDirectory = std::filesystem::path(internalStorage) / "assets";
    const std::filesystem::path shaderDirectory = paths.assetDirectory / "shaders";

    showStatus("1/6 C++ MAIN: OK\n2/6 SDL + JANELA: OK\n3/6 STORAGE: OK\n4/6 SDL GPU: iniciando...");
    SdlGpuRenderer renderer;
    if (!renderer.initialize(platform.nativeWindowHandle(), shaderDirectory.string())) {
        const std::string error = renderer.lastError();
        logError("SDL GPU initialization failed", error.c_str());
        showStatus("1/6 C++ MAIN: OK\n2/6 SDL + JANELA: OK\n3/6 STORAGE: OK\n4/6 SDL GPU: FALHOU\n\nERRO:\n" + error);
        platform.shutdown();
        return 1;
    }

    showStatus("1/6 C++ MAIN: OK\n2/6 SDL + JANELA: OK\n3/6 STORAGE: OK\n4/6 SDL GPU: OK\n5/6 ENGINE SIM: inicializando...");
    EngineSimApplication application;
    SdlAudioOutput audioOutput;
    application.initialize(&platform, &renderer, &audioOutput, paths);

    showStatus("1/6 C++ MAIN: OK\n2/6 SDL + JANELA: OK\n3/6 STORAGE: OK\n4/6 SDL GPU: OK\n5/6 ENGINE SIM: OK\n6/6 LOOP: INICIANDO\n\nRemovendo overlay...");
    SDL_Delay(700);
    hideDiagnostics();

    application.run();
    application.destroy();
    renderer.shutdown();
    platform.shutdown();
    return 0;
}
