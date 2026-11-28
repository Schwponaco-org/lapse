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
#include <cctype>
#include <climits>
#include <iterator>

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

static const unsigned short CP1252[128] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF
};

static const unsigned short CP1250[128] = {
    0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021,
    0x0000, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0000, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9
};

static const unsigned short ISO8859_2[128] = {
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009A, 0x009B, 0x009C, 0x009D, 0x009E, 0x009F,
    0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7,
    0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
    0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7,
    0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9
};

static const unsigned short CP1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F
};

static const char16_t* WESTERN[] = {
    u"æøåéÆØÅÉ",
    u"åäöéÅÄÖÉ",
    u"äöüßÄÖÜ",
    u"éèàçêâîôûëïùœÉÈÀÇÊÂÎÔÛËÏÙŒ",
    u"áéíóúñüÁÉÍÓÚÑÜ",
    u"àèéìòùÀÈÉÌÒÙ",
    u"ãõáéíóúâêôçàÃÕÁÉÍÓÚÂÊÔÇÀ",
    u"ëïéèöóËÏÉÈÖÓ",
};

static const char16_t* CENTRAL[] = {
    u"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ",
    u"áčďéěíňóřšťúůýžÁČĎÉĚÍŇÓŘŠŤÚŮÝŽ",
    u"áäčďéíľňóôšťúýžÁÄČĎÉÍĽŇÓÔŠŤÚÝŽ",
    u"áéíóöőúüűÁÉÍÓÖŐÚÜŰ",
    u"čćđšžČĆĐŠŽ",
    u"ăâîşţĂÂÎŞŢ",
};

static bool is_letter(unsigned c) {
    return (c < 0x80 && isalpha((int)c)) || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) ||
           (c >= 0x400 && c <= 0x4FF);
}

static bool read_page(const std::string& raw, const unsigned short* high, std::vector<unsigned>& text) {
    text.clear();
    for (unsigned char b : raw) {
        unsigned c = b < 0x80 ? b : high[b - 0x80];
        if (!c) return false;
        text.push_back(c);
    }
    return true;
}

static int fit(const std::vector<unsigned>& text, const char16_t* language) {
    std::u16string letters = language ? language : u"";
    int total = 0;
    for (size_t i = 0; i < text.size(); i++) {
        unsigned c = text[i];
        if (c < 0x80) continue;

        bool before = i > 0 && is_letter(text[i - 1]);
        bool after = i + 1 < text.size() && is_letter(text[i + 1]);
        if (!is_letter(c)) {
            if (c < 0xA0 || (before && after)) total -= 5;
            else if ((c == 0xBF || c == 0xA1) && after) total += 3;
        } else if (!language) {
            bool latin = (before && text[i - 1] < 0x80) || (after && text[i + 1] < 0x80);
            bool word = (before && text[i - 1] >= 0x400) || (after && text[i + 1] >= 0x400);
            if (latin || c < 0x400) total -= 1;
            else if (word) total += 3;
        } else if (before && after && text[i - 1] >= 0x80 && text[i + 1] >= 0x80) {
            total -= 1;
        } else {
            total += letters.find((char16_t)c) != std::u16string::npos ? 3 : -1;
        }
    }
    return total;
}

static const unsigned short* guess_page(const std::string& raw) {
    struct Page { const unsigned short* high; const char16_t* const* languages; size_t count; };
    static const Page pages[] = {
        {CP1252,    WESTERN, std::size(WESTERN)},
        {CP1250,    CENTRAL, std::size(CENTRAL)},
        {ISO8859_2, CENTRAL, std::size(CENTRAL)},
        {CP1251,    nullptr, 1},
    };

    const unsigned short* best = CP1252;
    int most = INT_MIN;
    std::vector<unsigned> text;
    for (const Page& page : pages) {
        if (!read_page(raw, page.high, text)) continue;
        for (size_t k = 0; k < page.count; k++) {
            int points = fit(text, page.languages ? page.languages[k] : nullptr);
            if (points > most) { most = points; best = page.high; }
        }
    }
    return best;
}

static std::string page_to_utf8(const std::string& raw, const unsigned short* high) {
    std::string out;
    for (unsigned char b : raw)
        push_utf8(out, b < 0x80 || !high[b - 0x80] ? b : high[b - 0x80]);
    return out;
}

static char to_cp1252(unsigned cp) {
    if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) return (char)cp;
    for (int i = 0; i < 32; i++)
        if (CP1252[i] == cp) return (char)(0x80 + i);
    return '?';
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

static std::string utf8_to_latin1(const std::string& text) {
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
                out += to_cp1252(built);
                i += extra + 1;
                continue;
            }
        }

        out += (char)c;
        i++;
    }
    return out;
}

static bool is_utf8(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        unsigned char c = text[i];
        if (c < 0x80) { i++; continue; }

        int extra;
        if (c > 0xF4) return false;
        else if (c >= 0xF0) extra = 3;
        else if (c >= 0xE0) extra = 2;
        else if (c >= 0xC2) extra = 1;
        else return false;

        if (i + extra >= text.size()) return false;
        for (int k = 1; k <= extra; k++)
            if (((unsigned char)text[i + k] & 0xC0) != 0x80) return false;

        unsigned char next = text[i + 1];
        if ((c == 0xE0 && next < 0xA0) || (c == 0xED && next > 0x9F) || (c == 0xF0 && next < 0x90) || (c == 0xF4 && next > 0x8F)) return false;

        i += extra + 1;
    }
    return true;
}

std::string make_utf8(const std::string& text) {
    return is_utf8(text) ? text : page_to_utf8(text, guess_page(text));
}

bool charset_from_name(const std::string& name, Charset* out) {
    std::string want;
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        if (c == '-' || c == '_' || c == ' ') continue;
        want += (char)tolower((unsigned char)c);
    }

    if (want == "utf8")                            *out = Charset::Legacy;
    else if (want == "utf8bom")                    *out = Charset::Utf8Bom;
    else if (want == "utf16" || want == "utf16le") *out = Charset::Utf16LeBom;
    else if (want == "utf16be")                    *out = Charset::Utf16BeBom;
    else if (want == "latin1" || want == "iso88591" || want == "cp1252") *out = Charset::Latin1;
    else return false;

    return true;
}

std::string decode(const std::string& raw, Charset how) {
    switch (how) {
        case Charset::Utf8Bom:    return raw.substr(3);
        case Charset::Utf16LeBom: return utf16_to_utf8(raw, 2, true);
        case Charset::Utf16BeBom: return utf16_to_utf8(raw, 2, false);
        case Charset::Utf16Le:    return utf16_to_utf8(raw, 0, true);
        case Charset::Utf16Be:    return utf16_to_utf8(raw, 0, false);
        case Charset::Latin1:     return page_to_utf8(raw, CP1252);
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
        case Charset::Latin1:     return utf8_to_latin1(utf8);
        default:                  return utf8;
    }
}
