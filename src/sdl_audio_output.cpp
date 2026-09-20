#include "../include/sdl_audio_output.h"
#include "../include/sdl_audio_util.h"
#include "../include/simulator.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>

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
    if (m_diagnostics) {
        SDL_AudioSpec source = {}, destination = {};
        if (SDL_GetAudioStreamFormat(m_stream, &source, &destination)) {
            std::fprintf(stderr, "audio: stream=%dHz/%dch -> device=%dHz/%dch\n",
                source.freq, source.channels, destination.freq, destination.channels);
        }
    }
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
    if (output == nullptr || !output->m_running || output->m_simulator == nullptr ||
        additionalAmount <= 0) {
        return;
    }
    output->fillStream(stream, additionalAmount);
}

void SdlAudioOutput::fillStream(SDL_AudioStream *stream, int requestedBytes) {
    if (stream == nullptr || m_simulator == nullptr || requestedBytes <= 0) return;

#if defined(__ANDROID__)
    constexpr int chunkFrames = 1024;
#else
    constexpr int chunkFrames = 512;
#endif
    constexpr int bytesPerFrame = static_cast<int>(sizeof(std::int16_t));
    std::array<std::int16_t, chunkFrames> samples{};

    int remainingBytes = requestedBytes;
    while (remainingBytes > 0) {
        const int frames = std::min(chunkFrames,
            (remainingBytes + bytesPerFrame - 1) / bytesPerFrame);
        const int pcmFrames = m_simulator->readAudioOutput(frames, samples.data());
        const int validFrames = std::max(0, pcmFrames);
        m_pcmFrames += validFrames;
        m_silenceFrames += frames - validFrames;

        const int bytes = frames * bytesPerFrame;
        if (!SDL_PutAudioStreamData(stream, samples.data(), bytes)) return;
        remainingBytes -= bytes;
    }

    if (m_diagnostics) {
        const std::uint64_t now = SDL_GetTicks();
        if (now - m_lastDiagnosticTick >= 1000) {
            std::fprintf(stderr,
                "audio-demand: pcm=%llu silence=%llu input=%.3fs output=%.3fs requested=%dB\n",
                static_cast<unsigned long long>(m_pcmFrames),
                static_cast<unsigned long long>(m_silenceFrames),
                m_simulator->getSynthesizerInputLatency(),
                m_simulator->getSynthesizerOutputLatency(),
                requestedBytes);
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
    m_running = false;
    if (m_stream != nullptr) {
        SDL_SetAudioStreamGetCallback(m_stream, nullptr, nullptr);
        SDL_DestroyAudioStream(m_stream);
    }
    m_stream = nullptr;
    m_simulator = nullptr;
}
