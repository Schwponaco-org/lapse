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

#pragma once
#include <string>

// what the file was when we picked it up so we can put it back the same way.
// legacy is any ascii compatible codepage - cp1251, gbk, big5, sjis. we never
// need to know which: we only touch the digits in a timestamp and those are
// ascii everywhere, the rest of the line goes back out untouched
enum class Charset {
    Legacy,
    Utf8Bom,
    Utf16Le,
    Utf16LeBom,
    Utf16Be,
    Utf16BeBom
};

Charset sniff(const std::string& raw);
std::string decode(const std::string& raw, Charset how);
std::string encode(const std::string& utf8, Charset how);
