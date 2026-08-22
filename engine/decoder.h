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
#include <iostream>
#include <vector>
#include <string>
#include <utility>
#include <cmath>
#include <fvad.h>


extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswresample/swresample.h>
}

AVFormatContext* open_file(const char* filename);
double probe_fps(const char* filename);
std::string embedded_text(const char* filename, int wanted = -1);
int find_audio_stream(const AVFormatContext* pFormatContext, int wanted = -1);
AVCodecContext* open_audio_decoder(const AVFormatContext* pFormatContext, int audio_stream_index);
std::vector<std::pair<int, int>> embedded_spans(AVFormatContext* pFormatContext, int wanted = -1);
std::vector<float> speech_profile(AVFormatContext* pFormatContext, AVCodecContext* dec_ctx, int audio_stream_index, int windows = 0, double* coverage = nullptr);
