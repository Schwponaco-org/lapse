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


int main(int argc, const char *argv[]) {
    if (argc < 3) {

        std::cerr << "Usage: lapse <video_or_subtitle> <subtitle.srt>\n";
        std::cerr << "Usage: lapse <video_or_subtitle> <subtitle>\n";
        return -1;
    }

    std::string reference_path = argv[1];
    std::string input_path = argv[2];

    // build reference activity profile
    std::vector<int> reference_activity;
    if (is_subtitle(reference_path)) {
        auto [ref_spans, _] = read_srt(reference_path.c_str());
        auto [ref_spans, _] = process_spans(read_subtitle(reference_path));
        reference_activity = activity(ref_spans);
    } else {
        AVFormatContext* AVC = open_file(reference_path.c_str());
        int audio_stream_index = find_audio_stream(AVC);
        AVCodecContext* OAD = open_audio_decoder(AVC, audio_stream_index);
        std::vector<float> decoded = decode_audio(AVC, OAD, audio_stream_index);
        std::vector<int16_t> pcm = convert_to_8kHz(decoded, OAD);
        decoded.clear(); decoded.shrink_to_fit();
        std::vector<float> fvad = calculate_fvad(pcm, 8000);
        pcm.clear(); pcm.shrink_to_fit();
        reference_activity = std::vector<int>(fvad.begin(), fvad.end());
    }

    auto [spans, mapping] = read_srt(input_path.c_str());
    if (spans.empty()) {
        std::cerr << "No timestamps found in SRT: " << input_path << '\n';

    auto [spans, mapping] = process_spans(read_subtitle(input_path));
    if (spans.empty()) {
        std::cerr << "No timestamps found in subtitle file: " << input_path << '\n';

        return 1;
    }
    std::vector<int> input_activity = activity(spans);

    auto [slope, intercept] = fft_crosscorrelate(reference_activity, input_activity);
    write_srt_OLS(input_path.c_str(), input_path.c_str(), slope, intercept);

    if (input_path.ends_with(".srt"))
        write_srt_OLS(input_path.c_str(), input_path.c_str(), slope, intercept);
    else if (input_path.ends_with(".ass") || input_path.ends_with(".ssa"))
        write_ass_OLS(input_path.c_str(), input_path.c_str(), slope, intercept);
    else if (input_path.ends_with(".vtt"))
        write_vtt_OLS(input_path.c_str(), input_path.c_str(), slope, intercept);
    
    std::cout << "Done: slope=" << slope << " intercept=" << intercept << "s -> " << input_path << '\n';

    return 0;
}
