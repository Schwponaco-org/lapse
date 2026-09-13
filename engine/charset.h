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

#pragma once
#include <string>

enum class Charset {
    Legacy,
    Utf8Bom,
    Utf16Le,
    Utf16LeBom,
    Utf16Be,
    Utf16BeBom,
    Latin1
};

Charset sniff(const std::string& raw);
bool charset_from_name(const std::string& name, Charset* out);
std::string make_utf8(const std::string& text);
std::string decode(const std::string& raw, Charset how);
std::string encode(const std::string& utf8, Charset how);
