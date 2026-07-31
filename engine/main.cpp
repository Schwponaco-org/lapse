#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include "decoder.h"
#include "srt_parser.h"
#include "correlate.h"

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: subsnap <video> <subtitle.srt>\n";
        return -1;
    }

    AVFormatContext* AVC = open_file(argv[1]);
    int audio_stream_index = find_audio_stream(AVC);
    AVCodecContext* OAD = open_audio_decoder(AVC, audio_stream_index);

    std::vector<float> decoded = decode_audio(AVC, OAD, audio_stream_index);
    std::vector<int16_t> pcm = convert_to_8kHz(decoded, OAD);
    decoded.clear();
    decoded.shrink_to_fit();
    std::vector<float> fvad = calculate_fvad(pcm, 8000);
    pcm.clear();
    pcm.shrink_to_fit();

    auto [spans, mapping] = read_srt(argv[2]);
    if (spans.empty()) {
        std::cerr << "No timestamps found in SRT: " << argv[2] << '\n';
        return 1;
    }
    std::vector<int> activity_variable = activity(spans);
    std::vector<int> fvad_int(fvad.begin(), fvad.end());

    /*
    Something:
    if (P == 0) {
        subsnap OLS pipeline (fft_cross_correlate)
    } else {
        run span-alignment with penalty P
    }
    */

    auto [slope, intercept] = fft_crosscorrelate(fvad_int, activity_variable);

    write_srt_OLS(argv[2], argv[2], slope, intercept);

    std::cout << "Done: slope=" << slope << " intercept=" << intercept << "s -> " << argv[2] << '\n';

    return 0;
}