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
#include <cstring>
#include <cmath>
#include "decoder.h"
#include "srt_parser.h"
#include "correlate.h"
#include "write_subtitle.h"

// The formats the parsers and the writers both handle. Callers can ask for the
// list with --formats so they know what is safe to hand us
const char* subtitle_formats[] = {".srt", ".ass", ".ssa", ".vtt"};

bool is_subtitle(const std::string& path) {
    for (auto& ext : subtitle_formats)
        if (path.size() > strlen(ext) && path.substr(path.size() - strlen(ext)) == ext)
            return true;
    return false;
}

// The format follows the file we read, the destination is wherever the caller asked us to put it
void write_offsets(const std::string& in_path, const std::string& out_path, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    if (in_path.ends_with(".srt"))
        write_srt_split(in_path.c_str(), out_path.c_str(), offsets, mapping);
    else if (in_path.ends_with(".ass") || in_path.ends_with(".ssa"))
        write_ass_split(in_path.c_str(), out_path.c_str(), offsets, mapping);
    else if (in_path.ends_with(".vtt"))
        write_vtt_split(in_path.c_str(), out_path.c_str(), offsets, mapping);
}

void usage() {
    std::cerr << "Usage: lapse <video_or_subtitle> <subtitle> [ols|nosplit|split] [penalty] [--output <path>] [--no-backup] [--no-embedded]\n";
    std::cerr << "       lapse --formats\n";
}

int run(int argc, const char *argv[]) {
    // Pull the flags out first, whatever is left over is positional like before
    std::vector<std::string> args;
    std::string output_path;
    bool make_backup = true;
    bool use_embedded = true;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--formats") {
            for (auto& ext : subtitle_formats)
                std::cout << ext << '\n';
            return 0;
        } else if (arg == "--output") {
            if (i + 1 >= argc) { usage(); return -1; }
            output_path = argv[++i];
        } else if (arg == "--no-backup") {
            make_backup = false;
        } else if (arg == "--no-embedded") {
            use_embedded = false;
        } else {
            args.push_back(arg);
        }
    }

    if (args.size() < 2) {
        usage();
        return -1;
    }

    std::string ref_path   = args[0];
    std::string input_path = args[1];
    std::string mode       = (args.size() >= 3) ? args[2] : "nosplit";
    float p                = (args.size() >= 4) ? std::stof(args[3]) : 6.0f;

    if (!is_subtitle(input_path)) {
        std::cerr << "Unsupported subtitle format: " << input_path << '\n';
        return 1;
    }

    // No --output means carry on overwriting the file we were given
    if (output_path.empty()) output_path = input_path;

    std::vector<int> reference_activity;
    std::vector<std::pair<int,int>> ref_spans;
    std::vector<float> ref_weights;

    if (is_subtitle(ref_path)) {
        auto [rs, _] = process_spans(read_subtitle(ref_path));
        ref_spans = rs;
        reference_activity = activity(ref_spans);
    } else {
        AVFormatContext* AVC = open_file(ref_path.c_str());
        if (!AVC) return 1;

        // A subtitle track inside the file is already exact, so there is no
        // reason to guess at the audio when one is sitting right there
        if (use_embedded) {
            auto [rs, _] = process_spans(embedded_spans(AVC));
            ref_spans = rs;
            reference_activity = activity(ref_spans);
        }

        if (ref_spans.empty()) {
            int audio_stream_index = find_audio_stream(AVC);
            AVCodecContext* OAD = open_audio_decoder(AVC, audio_stream_index);
            if (!OAD) {
                std::cerr << "No audio track we can decode in: " << ref_path << '\n';
                return 1;
            }

            std::vector<float> profile = speech_profile(AVC, OAD, audio_stream_index);
            if (profile.empty()) {
                std::cerr << "Got no audio out of: " << ref_path << '\n';
                return 1;
            }

            auto [rs, w] = reference_spans(profile);
            ref_spans = rs;
            ref_weights = w;

            reference_activity.reserve(profile.size());
            for (float p : profile) reference_activity.push_back(p >= 0.5f ? 1 : 0);
        }
    }

    if (ref_spans.empty()) {
        std::cerr << "No speech found in: " << ref_path << '\n';
        return 1;
    }

    auto [spans, mapping] = process_spans(read_subtitle(input_path), mode != "split");
    if (spans.empty()) {
        std::cerr << "No timestamps found in: " << input_path << '\n';
        return 1;
    }

    if (make_backup) backup_file(input_path.c_str());

    if (mode == "ols") {
        // Try the framerates people actually ship first. Only when none of them
        // fit do we go back to measuring the drift chunk by chunk, which is the
        // one thing that can still catch a stretch that isnt a standard ratio
        auto [ratio, offset, confidence] = best_framerate(spans, ref_spans, ref_weights);
        double slope = ratio - 1.0;
        double intercept = offset / 1000.0;

        if (confidence < 0.5) {
            std::cout << "No framerate fit well, measuring the drift instead\n";
            std::vector<int> input_activity = activity(spans);
            auto [s, i] = fft_crosscorrelate(reference_activity, input_activity);
            slope = s;
            intercept = i;
        }

        if (input_path.ends_with(".srt"))
            write_srt_OLS(input_path.c_str(), output_path.c_str(), slope, intercept);
        else if (input_path.ends_with(".ass") || input_path.ends_with(".ssa"))
            write_ass_OLS(input_path.c_str(), output_path.c_str(), slope, intercept);
        else if (input_path.ends_with(".vtt"))
            write_vtt_OLS(input_path.c_str(), output_path.c_str(), slope, intercept);
        std::cout << "Done (ols): slope=" << slope << " intercept=" << intercept << "s confidence=" << confidence << " -> " << output_path << '\n';

    } else if (mode == "nosplit") {
        auto [offset, confidence] = best_offset(spans, ref_spans, ref_weights);
        std::vector<int> offsets(spans.size(), offset);
        write_offsets(input_path, output_path, offsets, mapping);
        std::cout << "Done (nosplit): offset=" << offset << "ms confidence=" << confidence << " -> " << output_path << '\n';

    } else if (mode == "split") {
        // Lock onto the whole file first and let the split search work around
        // that it only has to look at the offsets near the one we found
        auto [offset, confidence] = best_offset(spans, ref_spans, ref_weights);
        std::vector<int> offsets = split_alignment(spans, ref_spans, ref_weights, p, offset);
        write_offsets(input_path, output_path, offsets, mapping);
        std::cout << "Done (split, p=" << p << "): base=" << offset << "ms confidence=" << confidence << " -> " << output_path << '\n';

    } else {
        std::cerr << "Unknown mode: " << mode << ". Use ols, nosplit or split.\n";
        return -1;
    }

    return 0;
}


int main(int argc, const char *argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
