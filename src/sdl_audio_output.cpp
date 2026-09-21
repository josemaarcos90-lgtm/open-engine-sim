#include "../include/sdl_audio_output.h"
#include "../include/sdl_audio_util.h"
#include "../include/simulator.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#if defined(__ANDROID__)
#include <jni.h>
#endif

bool SdlAudioOutput::start(Simulator *simulator) {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    stopLocked();
    if (simulator == nullptr) return false;
    // The synthesizer produces 44.1 kHz PCM. Keep this stream in that native
    // clock domain; SDL handles only the final conversion to the device rate.
    const SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, 44100 };
    m_simulator = simulator;
    m_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudioOutput::audioCallback, this);
    if (m_stream == nullptr) {
        m_simulator = nullptr;
        return false;
    }
    m_diagnostics = SDL_GetHintBoolean("ENGINE_SIM_AUDIO_DIAGNOSTICS", false);
    m_lastDiagnosticTick = SDL_GetTicks();
    m_pcmFrames = 0;
    m_silenceFrames = 0;
    m_peakQueuedBytes = 0;
    m_underrunEvents.store(0);
    m_clipEvents.store(0);
    m_lastVisualDiagnosticTick = 0;
    m_lastCallbackTick = 0;
    m_worstCallbackGap = 0;
    m_worstFillTime = 0;
    m_worstPutTime = 0;
    m_sourceBytesPerFrame = static_cast<int>(sizeof(std::int16_t));
    m_deviceFrequency = 0;
    if (m_diagnostics) {
        SDL_AudioSpec source = {}, destination = {};
        if (SDL_GetAudioStreamFormat(m_stream, &source, &destination)) {
            std::fprintf(stderr, "audio: stream=%dHz/%dch -> device=%dHz/%dch\n",
                source.freq, source.channels, destination.freq, destination.channels);
        }
    }
#if defined(__ANDROID__)
    // SDL's stream callback reports demand in bytes in the stream's SOURCE
    // format. Let SDL own resampling to the device's actual format/rate instead
    // of trying to infer a hardware queue from SDL_GetAudioStreamQueued().
    SDL_AudioSpec source = {}, destination = {};
    if (SDL_GetAudioStreamFormat(m_stream, &source, &destination)) {
        m_sourceBytesPerFrame = std::max(1,
            static_cast<int>(SDL_AUDIO_BYTESIZE(source.format)) * source.channels);
        m_deviceFrequency = destination.freq;
    }
#endif
    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        stop();
        return false;
    }
    m_running = true;
    return true;
}

void SDLCALL SdlAudioOutput::audioCallback(
    void *userdata, SDL_AudioStream *stream, int additionalAmount, int)
{
    auto *output = static_cast<SdlAudioOutput *>(userdata);
    if (output == nullptr) return;
    output->m_callbacksInFlight.fetch_add(1, std::memory_order_acq_rel);
#if defined(__ANDROID__)
    const std::uint64_t callbackNow = SDL_GetTicks();
    const std::uint64_t callbackGap = output->m_lastCallbackTick == 0
        ? 0 : callbackNow - output->m_lastCallbackTick;
    output->m_lastCallbackTick = callbackNow;
    output->m_worstCallbackGap = std::max(output->m_worstCallbackGap, callbackGap);
#endif
    if (output->m_running.load(std::memory_order_acquire) &&
        output->m_simulator != nullptr && additionalAmount > 0) {
        output->fillStream(stream, additionalAmount);
    }
    output->m_callbacksInFlight.fetch_sub(1, std::memory_order_acq_rel);
}

void SdlAudioOutput::fillStream(SDL_AudioStream *stream, int requestedBytes) {
    if (stream == nullptr || m_simulator == nullptr || requestedBytes <= 0) return;
#if defined(__ANDROID__)
    const std::uint64_t fillStartTick = SDL_GetTicks();
#endif

#if defined(__ANDROID__)
    constexpr int chunkFrames = 2048;
#else
    constexpr int chunkFrames = 512;
#endif
    constexpr int bytesPerFrame = static_cast<int>(sizeof(std::int16_t));
    std::array<std::int16_t, chunkFrames> samples{};

    // SDL can request a very small refill exactly when the convolution worker
    // is briefly late. Feed at least one Android block so the device stream
    // has useful headroom instead of repeatedly running on the edge.
    // additionalAmount is SDL's current source-side demand. Supplying exactly
    // that amount avoids the Alpha52 mistake of treating the stream input queue
    // as if it were the Android hardware queue. Keep only a modest one-block
    // cushion for callback jitter.
#if defined(__ANDROID__)
    const int remainingRequested = std::max(
        requestedBytes, chunkFrames * bytesPerFrame);
    int remainingBytes = std::min(
        remainingRequested, requestedBytes + chunkFrames * bytesPerFrame);
#else
    int remainingBytes = requestedBytes;
#endif
    while (remainingBytes > 0) {
        const int frames = std::min(chunkFrames,
            (remainingBytes + bytesPerFrame - 1) / bytesPerFrame);
#if defined(__ANDROID__)
        const std::uint64_t readStartTick = SDL_GetTicks();
#endif
        const int pcmFrames = m_simulator->readAudioOutput(frames, samples.data());
#if defined(__ANDROID__)
        const std::uint64_t readTime = SDL_GetTicks() - readStartTick;
        m_worstFillTime = std::max(m_worstFillTime, readTime);
#endif
        const int validFrames = std::max(0, pcmFrames);
        m_pcmFrames += validFrames;
        m_silenceFrames += frames - validFrames;

        bool clipped = false;
        int peak = 0;
        for (int i = 0; i < validFrames; ++i) {
            const int magnitude = std::abs(static_cast<int>(samples[i]));
            peak = std::max(peak, magnitude);
            if (magnitude >= 32760) clipped = true;
        }
        const bool underrun = validFrames < frames;
#if defined(__ANDROID__)
        const int dspJump = m_simulator->synthesizer().consumeDspJumpPeak();
#else
        const int dspJump = 0;
#endif
        const bool dspDiscontinuity = dspJump > 0;
        if (underrun) m_underrunEvents.fetch_add(1, std::memory_order_relaxed);
        if (clipped) m_clipEvents.fetch_add(1, std::memory_order_relaxed);

#if defined(__ANDROID__)
        if (underrun || clipped || dspDiscontinuity) {
            const std::uint64_t now = SDL_GetTicks();
            if (now - m_lastVisualDiagnosticTick >= 180) {
                m_lastVisualDiagnosticTick = now;
                JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
                jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
                if (env != nullptr && activity != nullptr) {
                    jclass cls = env->GetObjectClass(activity);
                    if (cls != nullptr) {
                        jmethodID method = env->GetMethodID(cls, "showAudioGlitch", "(Ljava/lang/String;)V");
                        if (method != nullptr) {
                            char diagnostic[160];
                            if (dspDiscontinuity) {
                                std::snprintf(diagnostic, sizeof(diagnostic),
                                    "DSP JUMP %d%s%s  PCM %d/%d  PEAK %d",
                                    dspJump,
                                    underrun ? " + UNDERRUN" : "",
                                    clipped ? " + CLIP" : "",
                                    validFrames, frames, peak);
                            }
                            else {
                                std::snprintf(diagnostic, sizeof(diagnostic),
                                    "%s%s  PCM %d/%d  PEAK %d",
                                    underrun ? "UNDERRUN" : "",
                                    (underrun && clipped) ? " + CLIP" : (clipped ? "CLIP" : ""),
                                    validFrames, frames, peak);
                            }
                            jstring text = env->NewStringUTF(diagnostic);
                            env->CallVoidMethod(activity, method, text);
                            env->DeleteLocalRef(text);
                        }
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        env->DeleteLocalRef(cls);
                    }
                }
            }
        }
#endif

        const int bytes = frames * bytesPerFrame;
#if defined(__ANDROID__)
        const std::uint64_t putStartTick = SDL_GetTicks();
#endif
        if (!SDL_PutAudioStreamData(stream, samples.data(), bytes)) return;
#if defined(__ANDROID__)
        const std::uint64_t putTime = SDL_GetTicks() - putStartTick;
        m_worstPutTime = std::max(m_worstPutTime, putTime);
#endif
        remainingBytes -= bytes;
    }

#if defined(__ANDROID__)
    const std::uint64_t fillTotal = SDL_GetTicks() - fillStartTick;
    // Report scheduling/SDL stalls even when PCM itself is perfectly valid.
    // Hold the worst measurements until displayed so a sub-second spike is not lost.
    const bool callbackStall = m_worstCallbackGap >= 35;
    const bool readStall = m_worstFillTime >= 10;
    const bool putStall = m_worstPutTime >= 10;
    const bool fillStall = fillTotal >= 20;
    if (callbackStall || readStall || putStall || fillStall) {
        const std::uint64_t now = SDL_GetTicks();
        if (now - m_lastVisualDiagnosticTick >= 180) {
            m_lastVisualDiagnosticTick = now;
            JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (env != nullptr && activity != nullptr) {
                jclass cls = env->GetObjectClass(activity);
                if (cls != nullptr) {
                    jmethodID method = env->GetMethodID(cls, "showAudioGlitch", "(Ljava/lang/String;)V");
                    if (method != nullptr) {
                        char diagnostic[192];
                        std::snprintf(diagnostic, sizeof(diagnostic),
                            "STALL CB %llums READ %llums PUT %llums TOTAL %llums",
                            static_cast<unsigned long long>(m_worstCallbackGap),
                            static_cast<unsigned long long>(m_worstFillTime),
                            static_cast<unsigned long long>(m_worstPutTime),
                            static_cast<unsigned long long>(fillTotal));
                        jstring text = env->NewStringUTF(diagnostic);
                        env->CallVoidMethod(activity, method, text);
                        env->DeleteLocalRef(text);
                    }
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    env->DeleteLocalRef(cls);
                }
            }
            m_worstCallbackGap = 0;
            m_worstFillTime = 0;
            m_worstPutTime = 0;
        }
    }
#endif

    if (m_diagnostics) {
        const std::uint64_t now = SDL_GetTicks();
        if (now - m_lastDiagnosticTick >= 1000) {
            std::fprintf(stderr,
                "audio-demand: pcm=%llu silence=%llu input=%.3fs output=%.3fs requested=%dB device=%dHz\n",
                static_cast<unsigned long long>(m_pcmFrames),
                static_cast<unsigned long long>(m_silenceFrames),
                m_simulator->getSynthesizerInputLatency(),
                m_simulator->getSynthesizerOutputLatency(),
                requestedBytes,
                m_deviceFrequency);
            m_pcmFrames = 0;
            m_silenceFrames = 0;
            m_lastDiagnosticTick = now;
        }
    }
}

bool SdlAudioOutput::loadImpulseResponse(Synthesizer &synthesizer, const std::string &path, float volume, int index) {
    return loadSdlImpulseResponse(synthesizer, path, volume, index);
}

void SdlAudioOutput::stop() {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    stopLocked();
}

void SdlAudioOutput::stopLocked() {
    // Hotload safety: publish the stopped state first, detach SDL's callback,
    // then wait for a callback that was already executing to leave before the
    // old Simulator can be destroyed by EngineSimApplication.
    m_running.store(false, std::memory_order_release);
    SDL_AudioStream *oldStream = m_stream;
    m_stream = nullptr;
    if (oldStream != nullptr) {
        SDL_SetAudioStreamGetCallback(oldStream, nullptr, nullptr);
        while (m_callbacksInFlight.load(std::memory_order_acquire) != 0) {
            SDL_Delay(1);
        }
        SDL_DestroyAudioStream(oldStream);
    }
    m_simulator = nullptr;
}
