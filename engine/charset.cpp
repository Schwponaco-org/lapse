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

#include "charset.h"
#include <vector>

static bool has(const std::string& raw, size_t n, const unsigned char* bytes) {
    if (raw.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)raw[i] != bytes[i]) return false;
    return true;
}


static Charset sniff_utf16(const std::string& raw) {
    size_t look = std::min<size_t>(raw.size(), 4096);
    if (look < 16) return Charset::Legacy;

    int even_nul = 0, odd_nul = 0;
    for (size_t i = 0; i < look; i++) {
        if (raw[i]) continue;
        if (i % 2) odd_nul++;
        else even_nul++;
    }

    int total = (int)look / 2;
    if (odd_nul > total * 3 / 10 && even_nul < total / 20) return Charset::Utf16Le;
    if (even_nul > total * 3 / 10 && odd_nul < total / 20) return Charset::Utf16Be;
    return Charset::Legacy;
}

Charset sniff(const std::string& raw) {
    static const unsigned char utf8_bom[] = {0xEF, 0xBB, 0xBF};
    static const unsigned char le_bom[]   = {0xFF, 0xFE};
    static const unsigned char be_bom[]   = {0xFE, 0xFF};

    if (has(raw, 3, utf8_bom)) return Charset::Utf8Bom;
    if (has(raw, 2, le_bom))   return Charset::Utf16LeBom;
    if (has(raw, 2, be_bom))   return Charset::Utf16BeBom;
    return sniff_utf16(raw);
}

static void push_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

static std::string utf16_to_utf8(const std::string& raw, size_t from, bool little) {
    std::string out;
    for (size_t i = from; i + 1 < raw.size(); i += 2) {
        unsigned lo = (unsigned char)raw[little ? i : i + 1];
        unsigned hi = (unsigned char)raw[little ? i + 1 : i];
        unsigned cp = (hi << 8) | lo;

        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < raw.size()) {
            unsigned lo2 = (unsigned char)raw[little ? i + 2 : i + 3];
            unsigned hi2 = (unsigned char)raw[little ? i + 3 : i + 2];
            unsigned tail = (hi2 << 8) | lo2;
            if (tail >= 0xDC00 && tail <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (tail - 0xDC00);
                i += 2;
            }
        }
        push_utf8(out, cp);
    }
    return out;
}

static void push_utf16(std::string& out, unsigned cp, bool little) {
    if (cp >= 0x10000) {
        cp -= 0x10000;
        push_utf16(out, 0xD800 + (cp >> 10), little);
        push_utf16(out, 0xDC00 + (cp & 0x3FF), little);
        return;
    }
    char lo = (char)(cp & 0xFF);
    char hi = (char)(cp >> 8);
    if (little) { out += lo; out += hi; }
    else        { out += hi; out += lo; }
}

// bad utf-8 goes through a byte at a time so a legacy codepage stays intact
static std::string utf8_to_utf16(const std::string& text, bool little) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = text[i];
        unsigned cp = c;
        int extra = 0;

        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }

        if (extra && i + extra < text.size()) {
            bool good = true;
            unsigned built = cp;
            for (int k = 1; k <= extra; k++) {
                unsigned char n = text[i + k];
                if ((n & 0xC0) != 0x80) { good = false; break; }
                built = (built << 6) | (n & 0x3F);
            }
            if (good) {
                push_utf16(out, built, little);
                i += extra + 1;
                continue;
            }
        }

        push_utf16(out, c, little);
        i++;
    }
    return out;
}

std::string decode(const std::string& raw, Charset how) {
    switch (how) {
        case Charset::Utf8Bom:    return raw.substr(3);
        case Charset::Utf16LeBom: return utf16_to_utf8(raw, 2, true);
        case Charset::Utf16BeBom: return utf16_to_utf8(raw, 2, false);
        case Charset::Utf16Le:    return utf16_to_utf8(raw, 0, true);
        case Charset::Utf16Be:    return utf16_to_utf8(raw, 0, false);
        default:                  return raw;
    }
}

std::string encode(const std::string& utf8, Charset how) {
    switch (how) {
        case Charset::Utf8Bom:    return "\xEF\xBB\xBF" + utf8;
        case Charset::Utf16LeBom: return "\xFF\xFE" + utf8_to_utf16(utf8, true);
        case Charset::Utf16BeBom: return std::string("\xFE\xFF") + utf8_to_utf16(utf8, false);
        case Charset::Utf16Le:    return utf8_to_utf16(utf8, true);
        case Charset::Utf16Be:    return utf8_to_utf16(utf8, false);
        default:                  return utf8;
    }
}
