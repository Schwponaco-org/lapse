// LAPSE - Language-Agnostic subtitle synchronization engine
// Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "silero.h"
#include "onnxruntime_c_api.h"
#include "log.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    typedef HMODULE library_handle;
#else
    #include <dlfcn.h>
    typedef void* library_handle;
#endif

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cstdlib>

static library_handle library = nullptr;
static const OrtApi* ort = nullptr;
static OrtEnv* env = nullptr;
static OrtSessionOptions* options = nullptr;
static OrtSession* session = nullptr;
static OrtMemoryInfo* memory = nullptr;
static bool tried = false;

// The model wants the tail of the previous chunk glued to the front of this one. Without it the numbers come back near zero for speech that is plainly there
static const int SILERO_CONTEXT = 64;

static const char* input_names[]  = {"input", "state", "sr"};
static const char* output_names[] = {"output", "stateN"};

static bool ok(OrtStatus* status) {
    if (!status) return true;
    std::cerr << "onnxruntime: " << ort->GetErrorMessage(status) << '\n';
    ort->ReleaseStatus(status);
    return false;
}

static std::filesystem::path binary_dir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        std::filesystem::path dir = std::filesystem::path(buffer).parent_path();
        if (!dir.empty()) return dir;
    }
#else
    Dl_info info;
    if (dladdr((void*)&binary_dir, &info) && info.dli_fname) {
        std::filesystem::path dir = std::filesystem::path(info.dli_fname).parent_path();
        if (!dir.empty()) return dir;
    }
#endif
    return std::filesystem::path(".");
}

static library_handle load_from(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path full = std::filesystem::absolute(path, ec);
    if (ec) full = path;
#ifdef _WIN32
    return LoadLibraryExW(full.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return dlopen(full.string().c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

static library_handle load_named(const char* name) {
#ifdef _WIN32
    return LoadLibraryA(name);
#else
    return dlopen(name, RTLD_LAZY | RTLD_LOCAL);
#endif
}

static void* library_symbol(library_handle handle, const char* name) {
#ifdef _WIN32
    return (void*)GetProcAddress(handle, name);
#else
    return dlsym(handle, name);
#endif
}

static library_handle load_runtime() {
#ifdef _WIN32
    const char* names[] = {"onnxruntime.dll"};
#elif defined(__APPLE__)
    const char* names[] = {"libonnxruntime.dylib", "libonnxruntime.1.dylib"};
#else
    const char* names[] = {"libonnxruntime.so", "libonnxruntime.so.1"};
#endif

    const char* forced = std::getenv("LAPSE_ONNXRUNTIME");
    if (forced) return load_from(forced);

    std::filesystem::path dir = binary_dir();
    for (const char* name : names) {
        library_handle handle = load_from(dir / name);
        if (handle) return handle;
    }
    for (const char* name : names) {
        library_handle handle = load_named(name);
        if (handle) return handle;
    }
    return nullptr;
}

static std::filesystem::path find_model() {
    std::vector<std::filesystem::path> paths;

    const char* forced = std::getenv("LAPSE_VAD_MODEL");
    if (forced) paths.push_back(forced);

    paths.push_back(binary_dir() / "silero_vad.onnx");
#ifndef _WIN32
    paths.push_back("/usr/local/share/lapse/silero_vad.onnx");
    paths.push_back("/usr/share/lapse/silero_vad.onnx");
#endif
    paths.push_back("silero_vad.onnx");

    for (const std::filesystem::path& path : paths) {
        std::ifstream file(path, std::ios::binary);
        if (file.good()) return path;
    }
    return {};
}

bool silero_open() {
    if (session) return true;
    if (tried) return false;
    tried = true;

    library = load_runtime();
    if (!library) {
        say() << "Silero VAD: no onnxruntime library found, using libfvad\n";
        return false;
    }

    typedef const OrtApiBase* (*ApiBase)(void);
    ApiBase base = (ApiBase)library_symbol(library, "OrtGetApiBase");
    if (!base) {
        say() << "Silero VAD: onnxruntime has no entry point, using libfvad\n";
        return false;
    }

    for (int version = ORT_API_VERSION; version >= 11 && !ort; version--)
        ort = base()->GetApi(version);
    if (!ort) {
        say() << "Silero VAD: onnxruntime is too old, using libfvad\n";
        return false;
    }

    std::filesystem::path model = find_model();
    if (model.empty()) {
        say() << "Silero VAD: no silero_vad.onnx found, using libfvad\n";
        return false;
    }

    if (!ok(ort->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "lapse", &env))) return false;
    if (!ok(ort->CreateSessionOptions(&options))) return false;
    ort->SetIntraOpNumThreads(options, 1);
    ort->SetInterOpNumThreads(options, 1);
    ort->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL);

    if (!ok(ort->CreateSession(env, model.c_str(), options, &session))) {
        session = nullptr;
        say() << "Silero VAD: could not load the model, using libfvad\n";
        return false;
    }
    if (!ok(ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory))) {
        session = nullptr;
        return false;
    }

    say() << "Silero VAD: " << model.string() << '\n';
    return true;
}

int silero_begin(Lanes& run, int lanes) {
    if (!session) return 0;
    if (lanes < 1) lanes = 1;

    run.count = lanes;
    run.state.assign(2 * lanes * 128, 0.0f);
    run.context.assign(lanes * SILERO_CONTEXT, 0.0f);
    run.window.assign(lanes * (SILERO_CONTEXT + SILERO_WINDOW), 0.0f);

    std::vector<float> probe(lanes * SILERO_WINDOW, 0.0f);
    std::vector<float> out(lanes, 0.0f);
    if (silero_step(run, probe.data(), out.data())) {
        run.state.assign(2 * lanes * 128, 0.0f);
        run.context.assign(lanes * SILERO_CONTEXT, 0.0f);
        return lanes;
    }

    // an older export with the batch axis nailed to one. fall back rather than die, it is only a speed thing
    if (lanes > 1) return silero_begin(run, 1);
    run.count = 0;
    return 0;
}

bool silero_step(Lanes& run, const float* pcm, float* out) {
    if (!session || run.count < 1) return false;
    int width = SILERO_CONTEXT + SILERO_WINDOW;
    std::vector<float>& state = run.state;
    std::vector<float>& context = run.context;
    std::vector<float>& window = run.window;
    int lane_count = run.count;

    for (int l = 0; l < lane_count; l++) {
        float* row = window.data() + (size_t)l * width;
        std::copy(context.begin() + (size_t)l * SILERO_CONTEXT,
                  context.begin() + (size_t)(l + 1) * SILERO_CONTEXT, row);
        std::copy(pcm + (size_t)l * SILERO_WINDOW, pcm + (size_t)(l + 1) * SILERO_WINDOW, row + SILERO_CONTEXT);
    }

    int64_t input_shape[2] = {lane_count, width};
    int64_t state_shape[3] = {2, lane_count, 128};
    int64_t scalar_shape[1] = {1};
    int64_t rate = SILERO_RATE;

    OrtValue* in[3] = {nullptr, nullptr, nullptr};
    OrtValue* res[2] = {nullptr, nullptr};
    bool done = false;

    bool ready =
        ok(ort->CreateTensorWithDataAsOrtValue(memory, window.data(), window.size() * sizeof(float), input_shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[0])) &&
        ok(ort->CreateTensorWithDataAsOrtValue(memory, state.data(), state.size() * sizeof(float), state_shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[1])) &&
        ok(ort->CreateTensorWithDataAsOrtValue(memory, &rate, sizeof(int64_t), scalar_shape, 0, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in[2]));

    if (ready && ok(ort->Run(session, nullptr, input_names, in, 3, output_names, 2, res))) {
        float* score = nullptr;
        float* next = nullptr;
        if (ok(ort->GetTensorMutableData(res[0], (void**)&score)) &&
            ok(ort->GetTensorMutableData(res[1], (void**)&next))) {
            for (int l = 0; l < lane_count; l++) {
                out[l] = score[l];
                const float* row = window.data() + (size_t)l * width;
                std::copy(row + width - SILERO_CONTEXT, row + width,
                          context.begin() + (size_t)l * SILERO_CONTEXT);
            }
            std::copy(next, next + state.size(), state.begin());
            done = true;
        }
    }

    for (OrtValue* value : res) if (value) ort->ReleaseValue(value);
    for (OrtValue* value : in) if (value) ort->ReleaseValue(value);
    return done;
}

void silero_close() {
    if (!ort) return;
    if (memory) ort->ReleaseMemoryInfo(memory);
    if (session) ort->ReleaseSession(session);
    if (options) ort->ReleaseSessionOptions(options);
    if (env) ort->ReleaseEnv(env);
    memory = nullptr;
    session = nullptr;
    options = nullptr;
    env = nullptr;
    ort = nullptr;

    if (library) {
#ifdef _WIN32
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        library = nullptr;
    }
    tried = false;
}
