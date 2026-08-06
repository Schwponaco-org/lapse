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

#include <dlfcn.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdlib>

static void* library = nullptr;
static const OrtApi* ort = nullptr;
static OrtEnv* env = nullptr;
static OrtSessionOptions* options = nullptr;
static OrtSession* session = nullptr;
static OrtMemoryInfo* memory = nullptr;
static std::vector<float> state;
static std::vector<float> context;
static std::vector<float> window;
static bool tried = false;

// The model wants the tail of the previous chunk glued to the front of this
// one. Without it the numbers come back near zero for speech that is plainly there
static const int SILERO_CONTEXT = 32;

static const char* input_names[]  = {"input", "state", "sr"};
static const char* output_names[] = {"output", "stateN"};

static bool ok(OrtStatus* status) {
    if (!status) return true;
    std::cerr << "onnxruntime: " << ort->GetErrorMessage(status) << '\n';
    ort->ReleaseStatus(status);
    return false;
}

static std::string binary_dir() {
    Dl_info info;
    if (dladdr((void*)&binary_dir, &info) && info.dli_fname) {
        std::string path = info.dli_fname;
        size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) return path.substr(0, slash);
    }
    return ".";
}

static void* load_runtime() {
    const char* names[] = {"libonnxruntime.so", "libonnxruntime.so.1", "libonnxruntime.dylib"};

    const char* forced = std::getenv("LAPSE_ONNXRUNTIME");
    if (forced) {
        void* handle = dlopen(forced, RTLD_LAZY | RTLD_LOCAL);
        if (handle) return handle;
        return nullptr;
    }

    std::string dir = binary_dir();
    for (const char* name : names) {
        std::string beside = dir + "/" + name;
        void* handle = dlopen(beside.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (handle) return handle;
    }
    for (const char* name : names) {
        void* handle = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (handle) return handle;
    }
    return nullptr;
}

static std::string find_model() {
    std::vector<std::string> paths;

    const char* forced = std::getenv("LAPSE_VAD_MODEL");
    if (forced) paths.push_back(forced);

    paths.push_back(binary_dir() + "/silero_vad.onnx");
    paths.push_back("/usr/local/share/lapse/silero_vad.onnx");
    paths.push_back("/usr/share/lapse/silero_vad.onnx");
    paths.push_back("silero_vad.onnx");

    for (const std::string& path : paths) {
        std::ifstream file(path, std::ios::binary);
        if (file.good()) return path;
    }
    return "";
}

bool silero_open() {
    if (session) return true;
    if (tried) return false;
    tried = true;

    library = load_runtime();
    if (!library) {
        std::cout << "Silero VAD: no onnxruntime library found, using libfvad\n";
        return false;
    }

    typedef const OrtApiBase* (*ApiBase)(void);
    ApiBase base = (ApiBase)dlsym(library, "OrtGetApiBase");
    if (!base) {
        std::cout << "Silero VAD: onnxruntime has no entry point, using libfvad\n";
        return false;
    }

    for (int version = ORT_API_VERSION; version >= 11 && !ort; version--)
        ort = base()->GetApi(version);
    if (!ort) {
        std::cout << "Silero VAD: onnxruntime is too old, using libfvad\n";
        return false;
    }

    std::string model = find_model();
    if (model.empty()) {
        std::cout << "Silero VAD: no silero_vad.onnx found, using libfvad\n";
        return false;
    }

    if (!ok(ort->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "lapse", &env))) return false;
    if (!ok(ort->CreateSessionOptions(&options))) return false;
    ort->SetIntraOpNumThreads(options, 1);
    ort->SetInterOpNumThreads(options, 1);
    ort->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL);

    if (!ok(ort->CreateSession(env, model.c_str(), options, &session))) {
        session = nullptr;
        std::cout << "Silero VAD: could not load the model, using libfvad\n";
        return false;
    }
    if (!ok(ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory))) {
        session = nullptr;
        return false;
    }

    state.assign(2 * 128, 0.0f);
    context.assign(SILERO_CONTEXT, 0.0f);
    std::cout << "Silero VAD: " << model << '\n';
    return true;
}

void silero_reset() {
    std::fill(state.begin(), state.end(), 0.0f);
    std::fill(context.begin(), context.end(), 0.0f);
}

float silero_run(const float* samples, int count) {
    if (!session) return -1.0f;

    window.resize(SILERO_CONTEXT + count);
    std::copy(context.begin(), context.end(), window.begin());
    std::copy(samples, samples + count, window.begin() + SILERO_CONTEXT);

    int64_t input_shape[2] = {1, (int64_t)window.size()};
    int64_t state_shape[3] = {2, 1, 128};
    int64_t scalar_shape[1] = {1};
    int64_t rate = 8000;

    OrtValue* in[3] = {nullptr, nullptr, nullptr};
    OrtValue* out[2] = {nullptr, nullptr};
    float probability = -1.0f;

    bool ready =
        ok(ort->CreateTensorWithDataAsOrtValue(memory, window.data(), window.size() * sizeof(float),
                                               input_shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[0])) &&
        ok(ort->CreateTensorWithDataAsOrtValue(memory, state.data(), state.size() * sizeof(float),
                                               state_shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[1])) &&
        ok(ort->CreateTensorWithDataAsOrtValue(memory, &rate, sizeof(int64_t),
                                               scalar_shape, 0, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in[2]));

    if (ready && ok(ort->Run(session, nullptr, input_names, in, 3, output_names, 2, out))) {
        float* score = nullptr;
        float* next = nullptr;
        if (ok(ort->GetTensorMutableData(out[0], (void**)&score)) &&
            ok(ort->GetTensorMutableData(out[1], (void**)&next))) {
            probability = score[0];
            std::copy(next, next + state.size(), state.begin());
            std::copy(window.end() - SILERO_CONTEXT, window.end(), context.begin());
        }
    }

    for (OrtValue* value : out) if (value) ort->ReleaseValue(value);
    for (OrtValue* value : in) if (value) ort->ReleaseValue(value);
    return probability;
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
}
