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
#include "decoder.h"
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
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

static std::string preset(const std::string& model) {
    static const char* known[] = {"tiny", "tiny.en", "base", "base.en", "small", "small.en", "medium", "medium.en",
                                  "large-v1", "large-v2", "large-v3", "large-v3-turbo"};
    std::string name = std::filesystem::path(model).filename().string();
    if (name.rfind("ggml-", 0) == 0) name = name.substr(5);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) name.resize(name.size() - 4);
    size_t quantized = name.rfind("-q");
    if (quantized != std::string::npos && isdigit((unsigned char)name[quantized + 2])) name.resize(quantized);
    for (const char* k : known)
        if (name == k) {
            std::replace(name.begin(), name.end(), '-', '.');
            return name;
        }
    return "";
}

static const unsigned not_letters[][2] = {
    {0x80, 0xBF}, {0xD7, 0xD7}, {0xF7, 0xF7}, {0x60C, 0x60C}, {0x61B, 0x61F}, {0x964, 0x965},
    {0x2000, 0x2BFF}, {0x3000, 0x303F}, {0xFE10, 0xFE6F}, {0xFF00, 0xFF0F}, {0xFF1A, 0xFF20},
    {0xFF3B, 0xFF40}, {0xFF5B, 0xFF65}, {0x1F000, 0x1FAFF},
};

static bool has_letter(const std::string& text) {
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = text[i];
        if (c < 0x80) {
            if (isalnum(c)) return true;
            continue;
        }

        int extra = c >= 0xF8 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (!extra || i + extra >= text.size()) continue;
        unsigned code = c & (0x3F >> extra);
        bool whole = true;
        for (int k = 1; k <= extra; k++) {
            unsigned char next = text[i + k];
            if ((next & 0xC0) != 0x80) whole = false;
            code = code << 6 | (next & 0x3F);
        }
        if (!whole) continue;
        i += extra;

        bool symbol = false;
        for (auto& range : not_letters)
            if (code >= range[0] && code <= range[1]) symbol = true;
        if (!symbol) return true;
    }
    return false;
}

static size_t closing_quote(const std::string& json, size_t from) {
    size_t end = from;
    while ((end = json.find('"', end)) != std::string::npos) {
        size_t slashes = 0;
        while (end - slashes > from && json[end - slashes - 1] == '\\') slashes++;
        if (slashes % 2 == 0) break;
        end++;
    }
    return end;
}

std::vector<int> spoken_words(const std::string& wav, const std::string& model) {
    std::string dtw = preset(model);
    if (dtw.empty()) {
        std::cerr << "Cannot tell which whisper model " << model << " is from its name, it should look like ggml-base.en.bin\n";
        return {};
    }

    const char* cli = std::getenv("LAPSE_WHISPER_CLI");
    int threads = worker_threads();
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

    std::vector<int> words, held;
    bool label = false;
    size_t at = 0;
    while ((at = json.find("\"text\": \"", at)) != std::string::npos) {
        at += 9;
        size_t end = closing_quote(json, at);
        if (end == std::string::npos) break;
        std::string text = json.substr(at, end - at);
        size_t t = json.find("\"t_dtw\": ", end);
        size_t next = json.find("\"text\": \"", end);
        if (t == std::string::npos || (next != std::string::npos && t > next)) {
            words.insert(words.end(), held.begin(), held.end());
            held.clear();
            label = false;
            continue;
        }
        if (text.rfind("[_", 0) == 0 || text.rfind("<|", 0) == 0) continue;

        long when = strtol(json.c_str() + t + 9, nullptr, 10);
        if (text.find_first_of("[(") != std::string::npos) label = true;
        if (has_letter(text) && when >= 0) (label ? held : words).push_back((int)(when * 10));
        if (label && text.find_first_of("])") != std::string::npos) {
            label = false;
            held.clear();
        }
    }
    words.insert(words.end(), held.begin(), held.end());
    std::sort(words.begin(), words.end());
    return words;
}
