// LAPSE - Language-Agnostic subtitle synchronization engine
// Copyright (C) 2026 Rasmus Stisen Jensen (r-stisen)
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

#include "cuetext.h"
#include "srt_parser.h"
#include <cstring>

// brackets a sound effect gets written in. the wide ones are three bytes in
// utf-8 so they are matched as strings
static const char* open_bracket[]  = {"[", "(", "\xef\xbc\x88", "\xe3\x80\x90", "\xe3\x80\x8c", "<<"};
static const char* close_bracket[] = {"]", ")", "\xef\xbc\x89", "\xe3\x80\x91", "\xe3\x80\x8d", ">>"};

// nothing here is ever spoken out loud
static const char* note_marks[] = {"\xe2\x99\xaa", "\xe2\x99\xab", "\xe2\x99\xac", "\xe2\x99\xa9", "#", "*", "~"};

static bool starts_with(const std::string& s, const char* what) {
    return s.compare(0, strlen(what), what) == 0;
}

static bool ends_with(const std::string& s, const char* what) {
    size_t n = strlen(what);
    return s.size() >= n && s.compare(s.size() - n, n, what) == 0;
}

// drops <i>, {\an8} and the rest so what is left is the words
static std::string strip_markup(const std::string& in) {
    std::string out;
    int angle = 0, brace = 0;
    for (char c : in) {
        if (c == '<') { angle++; continue; }
        if (c == '>') { if (angle) angle--; continue; }
        if (c == '{') { brace++; continue; }
        if (c == '}') { if (brace) brace--; continue; }
        if (!angle && !brace) out += c;
    }
    return out;
}

static std::string strip_dashes(const std::string& in) {
    std::string out = in;
    while (!out.empty() && (out[0] == '-' || out[0] == ' ' || out[0] == '\t')) out.erase(0, 1);
    return trim(out);
}

static bool only_notes(const std::string& s) {
    size_t i = 0;
    int letters = 0;
    while (i < s.size()) {
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n') { i++; continue; }
        bool hit = false;
        for (const char* m : note_marks) {
            if (s.compare(i, strlen(m), m) == 0) { i += strlen(m); hit = true; break; }
        }
        if (!hit) { letters++; i++; }
    }
    return letters == 0;
}

static bool junk_line(const std::string& raw) {
    std::string s = strip_dashes(strip_markup(raw));
    if (s.empty()) return true;
    if (only_notes(s)) return true;

    for (int b = 0; b < (int)(sizeof(open_bracket) / sizeof(*open_bracket)); b++) {
        if (starts_with(s, open_bracket[b]) && ends_with(s, close_bracket[b])) return true;
        // "[door slams]" with the closing bracket lost somewhere is still not speech
        if (starts_with(s, open_bracket[b]) && s.find(close_bracket[b]) == std::string::npos) return true;
    }

    size_t colon = s.find(':');
    if (colon != std::string::npos && trim(s.substr(colon + 1)).empty()) {
        std::string who = trim(s.substr(0, colon));
        bool shouty = !who.empty();
        for (char c : who) if (c >= 'a' && c <= 'z') shouty = false;
        if (shouty) return true;
    }
    return false;
}

bool is_junk_cue(const std::string& text) {
    if (trim(text).empty()) return true;

    std::string line;
    bool any_real = false;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i == text.size() || text[i] == '\n') {
            if (!trim(line).empty() && !junk_line(line)) any_real = true;
            line.clear();
        } else if (text[i] != '\r') {
            line += text[i];
        }
    }
    return !any_real;
}

static std::vector<std::string> srt_cue_text(const std::string& path) {
    std::vector<std::string> out;
    std::istringstream file(load_text(path));
    std::string line, blob;
    bool inside = false;

    while (getline(file, line)) {
        if (line.find("-->") != std::string::npos) {
            if (inside) out.push_back(blob);
            blob.clear();
            inside = true;
            continue;
        }
        if (!inside) continue;
        if (trim(line).empty()) {
            out.push_back(blob);
            blob.clear();
            inside = false;
            continue;
        }
        if (!blob.empty()) blob += '\n';
        blob += line;
    }
    if (inside) out.push_back(blob);
    return out;
}

// the text is the tail behind the last field the Format line named
static std::vector<std::string> ass_cue_text(const std::string& path) {
    std::vector<std::string> out;
    std::istringstream file(load_text(path));
    std::string line;
    int fields = 9;

    while (getline(file, line)) {
        if (line.rfind("Format:", 0) == 0) {
            int n = (int)ass_commas(line).size();
            if (n > 0) fields = n;
            continue;
        }
        if (line.rfind("Dialogue:", 0) != 0) continue;

        std::vector<size_t> commas = ass_commas(line);
        if ((int)commas.size() < fields) { out.push_back(line); continue; }

        std::string body = line.substr(commas[fields - 1] + 1);
        // ass writes line breaks as \N
        std::string flat;
        for (size_t i = 0; i < body.size(); i++) {
            if (body[i] == '\\' && i + 1 < body.size() && (body[i+1] == 'N' || body[i+1] == 'n')) {
                flat += '\n';
                i++;
            } else {
                flat += body[i];
            }
        }
        out.push_back(flat);
    }
    return out;
}

std::vector<std::string> read_cue_text(const std::string& path) {
    if (path.ends_with(".ass") || path.ends_with(".ssa")) return ass_cue_text(path);
    return srt_cue_text(path);
}

std::vector<std::pair<int,int>> drop_junk_cues(const std::vector<std::pair<int,int>>& timestamps, const std::vector<std::string>& text, int* dropped) {
    if (text.size() != timestamps.size()) {
        if (dropped) *dropped = 0;
        return timestamps;
    }

    std::vector<std::pair<int,int>> kept = timestamps;
    int gone = 0;
    int real = 0;
    for (size_t i = 0; i < kept.size(); i++) {
        if (kept[i].second <= kept[i].first) continue;
        if (is_junk_cue(text[i])) { kept[i] = {0, 0}; gone++; }
        else real++;
    }

    if (real < MIN_CUES) {
        if (dropped) *dropped = 0;
        return timestamps;
    }

    if (dropped) *dropped = gone;
    return kept;
}
