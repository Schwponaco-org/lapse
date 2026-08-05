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

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include "decoder.h"
#include "srt_parser.h"
#include "correlate.h"
#include "write_subtitle.h"

bool is_subtitle(const std::string& path) {
    for (auto& ext : {".srt", ".ass", ".ssa", ".vtt"})
        if (path.size() > strlen(ext) && path.substr(path.size() - strlen(ext)) == ext)
            return true;
    return false;
}

void write_offsets(const std::string& path, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    if (path.ends_with(".srt"))
        write_srt_split(path.c_str(), path.c_str(), offsets, mapping);
    else if (path.ends_with(".ass") || path.ends_with(".ssa"))
        write_ass_split(path.c_str(), path.c_str(), offsets, mapping);
    else if (path.ends_with(".vtt"))
        write_vtt_split(path.c_str(), path.c_str(), offsets, mapping);
}

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: lapse <video_or_subtitle> <subtitle> [ols|nosplit|split] [penalty]\n";
        return -1;
    }

    std::string ref_path   = argv[1];
    std::string input_path = argv[2];
    std::string mode       = (argc >= 4) ? argv[3] : "nosplit";
    float p                = (argc >= 5) ? std::stof(argv[4]) : 6.0f;

    std::vector<int> reference_activity;
    std::vector<std::pair<int,int>> ref_spans;

    if (is_subtitle(ref_path)) {
        auto [rs, _] = process_spans(read_subtitle(ref_path));
        ref_spans = rs;
        reference_activity = activity(ref_spans);
    } else {
        AVFormatContext* AVC = open_file(ref_path.c_str());
        int audio_stream_index = find_audio_stream(AVC);
        AVCodecContext* OAD = open_audio_decoder(AVC, audio_stream_index);
        std::vector<float> decoded = decode_audio(AVC, OAD, audio_stream_index);
        std::vector<int16_t> pcm = convert_to_8kHz(decoded, OAD);
        decoded.clear(); decoded.shrink_to_fit();
        std::vector<float> fvad = calculate_fvad(pcm, 8000);
        pcm.clear(); pcm.shrink_to_fit();
        reference_activity = std::vector<int>(fvad.begin(), fvad.end());
        ref_spans = reference_spans(reference_activity);
    }

    auto [spans, mapping] = process_spans(read_subtitle(input_path));
    if (spans.empty()) {
        std::cerr << "No timestamps found in: " << input_path << '\n';
        return 1;
    }

    if (mode == "ols") {
        std::vector<int> input_activity = activity(spans);
        auto [slope, intercept] = fft_crosscorrelate(reference_activity, input_activity);
        if (input_path.ends_with(".srt"))
            write_srt_OLS(input_path.c_str(), input_path.c_str(), slope, intercept);
        else if (input_path.ends_with(".ass") || input_path.ends_with(".ssa"))
            write_ass_OLS(input_path.c_str(), input_path.c_str(), slope, intercept);
        else if (input_path.ends_with(".vtt"))
            write_vtt_OLS(input_path.c_str(), input_path.c_str(), slope, intercept);
        std::cout << "Done (ols): slope=" << slope << " intercept=" << intercept << "s -> " << input_path << '\n';

    } else if (mode == "nosplit") {
        int offset = best_offset(spans, ref_spans);
        std::vector<int> offsets(spans.size(), offset);
        write_offsets(input_path, offsets, mapping);
        std::cout << "Done (nosplit): offset=" << offset << "ms -> " << input_path << '\n';

    } else if (mode == "split") {
        std::vector<int> offsets = split_alignment(spans, ref_spans, p);
        write_offsets(input_path, offsets, mapping);
        std::cout << "Done (split, p=" << p << "): " << input_path << '\n';

    } else {
        std::cerr << "Unknown mode: " << mode << ". Use ols, nosplit or split.\n";
        return -1;
    }

    return 0;
}