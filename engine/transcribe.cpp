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

#include "transcribe.h"
#include "log.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>
#include <cctype>
#include <iostream>

static std::string in_quotes(const std::string& s) {
    return "\"" + s + "\"";
}

static std::string preset(const std::string& model) {
    static const char* known[] = {"tiny", "tiny.en", "base", "base.en", "small", "small.en", "medium", "medium.en",
                                  "large-v1", "large-v2", "large-v3", "large-v3-turbo"};
    std::string name = std::filesystem::path(model).filename().string();
    if (name.rfind("ggml-", 0) == 0) name = name.substr(5);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) name.resize(name.size() - 4);
    for (const char* k : known)
        if (name == k) return name;
    return "";
}

std::vector<int> spoken_words(const std::string& wav, const std::string& model) {
    std::string dtw = preset(model);
    if (dtw.empty()) {
        std::cerr << "Cannot tell which whisper model " << model << " is from its name, it should look like ggml-base.en.bin\n";
        return {};
    }

    const char* cli = std::getenv("LAPSE_WHISPER_CLI");
    int threads = std::clamp((int)std::thread::hardware_concurrency(), 1, 8);
    std::string out = (std::filesystem::temp_directory_path() /
        ("lapse-words-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();

    std::string command = in_quotes(cli ? cli : "whisper-cli") + " -m " + in_quotes(model) + " -f " + in_quotes(wav) +
        " -ojf -of " + in_quotes(out) + " -dtw " + dtw + " -nfa -np -t " + std::to_string(threads) +
        " -l " + (dtw.size() > 3 && dtw.compare(dtw.size() - 3, 3, ".en") == 0 ? "en" : "auto");
#ifdef _WIN32
    command = "\"" + command + " > NUL 2>&1\"";
#else
    command += " > /dev/null 2>&1";
#endif

    say() << "Listening for words with " << model << ", this takes a while\n";
    int status = std::system(command.c_str());

    std::ifstream in(out + ".json");
    std::stringstream all;
    all << in.rdbuf();
    std::string json = all.str();
    in.close();
    std::error_code ec;
    std::filesystem::remove(out + ".json", ec);

    if (status != 0 || json.empty()) {
        std::cerr << "whisper-cli did not run, put it on the PATH or point LAPSE_WHISPER_CLI at it\n";
        return {};
    }

    std::vector<int> words;
    size_t at = 0;
    while ((at = json.find("\"text\": \"", at)) != std::string::npos) {
        at += 9;
        size_t end = json.find('"', at);
        std::string text = json.substr(at, end - at);
        size_t t = json.find("\"t_dtw\": ", end);
        size_t next = json.find("\"text\": \"", end);
        if (t == std::string::npos || (next != std::string::npos && t > next)) continue;

        long when = strtol(json.c_str() + t + 9, nullptr, 10);
        bool word = text.rfind("[_", 0) != 0 && text.rfind("<|", 0) != 0 &&
                    std::any_of(text.begin(), text.end(), [](char c) { return isalnum((unsigned char)c); });
        if (word && when >= 0) words.push_back((int)(when * 10));
    }
    std::sort(words.begin(), words.end());
    return words;
}
