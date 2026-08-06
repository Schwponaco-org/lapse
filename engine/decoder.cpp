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

#include "decoder.h"

// Opens the file, allocate memory, finds stream info. If returns nullptr cause the function is pointer
AVFormatContext* open_file(const char* filename) {
        AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext) {
        std::cerr << "ERROR could not allocate memory for Format Context" << '\n';
        return nullptr;
    }

    if (avformat_open_input(&pFormatContext, filename, NULL, NULL) != 0) {
        std::cerr << "ERROR could not open the file" << '\n';
        return nullptr;
    }
    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
        std::cerr << "ERROR could not get stream info" << '\n';
        return nullptr;
    }

    std::cout << "Format: " << pFormatContext->iformat->name << " Duration: " << pFormatContext->duration << '\n';
    return pFormatContext;
}

// Find the audio by looping though the streams from pFormatContext then checking by matching with the media type.
// Take the one ffmpeg marked as default if there is one - the first audio track
// is often a commentary or a dub and syncing to that gives the wrong answer
int find_audio_stream(const AVFormatContext* pFormatContext) {
    int first = -1;
    for(int i {0}; i < (int)pFormatContext->nb_streams; ++i) {
        if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (first < 0) first = i;
            if (pFormatContext->streams[i]->disposition & AV_DISPOSITION_DEFAULT) return i;
        }

    }
    return first;
}

// This is supposed to decode and open the audio from find_audio_stream()
AVCodecContext* open_audio_decoder(const AVFormatContext* pFormatContext, int audio_stream_index) {
    // Casually checking if we can open the decoder ))
    if (audio_stream_index <0) {
        std::cerr << "Error opening the decoder: " << '\n';
        return nullptr;
    }

    // Look for the same codec de decode
    const AVCodec *decoder = avcodec_find_decoder(pFormatContext->streams[audio_stream_index]->codecpar->codec_id);

    if (!decoder) {
        std::cerr << "Necessary decoder not found" << '\n';
        return nullptr;
    }
    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        std::cerr << "Failed to allocate decoder context" << '\n';
        return nullptr;
    }
    // This to get value from AVCodecParameters to the AVCodecContext so avcodec_open2 can work with it
    avcodec_parameters_to_context(dec_ctx, pFormatContext->streams[audio_stream_index]->codecpar);
    dec_ctx->thread_count = 0;   // let ffmpeg use every core it wants

    int ret = avcodec_open2(dec_ctx, NULL, NULL);
    if (ret < 0) {
        std::cerr << "Cannot open audio decoder" << '\n';
        return nullptr;
    }
    return dec_ctx;
}

// Grabs one sample as a float no matter which of the usual formats we got back.
// Planar keeps every channel in its own buffer, packed interleaves them
static float sample_at(const AVFrame* frame, int channel, int index) {
    int channels = frame->ch_layout.nb_channels;
    switch (frame->format) {
        case AV_SAMPLE_FMT_FLTP:
            return ((const float*)frame->data[channel])[index];
        case AV_SAMPLE_FMT_FLT:
            return ((const float*)frame->data[0])[index * channels + channel];
        case AV_SAMPLE_FMT_S16P:
            return ((const int16_t*)frame->data[channel])[index] / 32768.0f;
        case AV_SAMPLE_FMT_S16:
            return ((const int16_t*)frame->data[0])[index * channels + channel] / 32768.0f;
        default:
            return 0.0f;
    }
}

// Decode the audio and squash it down to 8kHz mono on the way out. libfvad only
// wants 8kHz anyway and holding a whole film as floats first was eating well
// over a gigabyte before we threw almost all of it away again
std::vector<int16_t> decode_audio(AVFormatContext* pFormatContext, AVCodecContext* dec_ctx ,int audio_stream_index) {
    int ret;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    std::vector<int16_t> decoded = {};

    if (!packet || !frame) {
        std::cerr << "Could not allocate frame or packet" << '\n';
        return {};
    }

    if (dec_ctx->sample_fmt != AV_SAMPLE_FMT_FLTP && dec_ctx->sample_fmt != AV_SAMPLE_FMT_FLT &&
        dec_ctx->sample_fmt != AV_SAMPLE_FMT_S16P && dec_ctx->sample_fmt != AV_SAMPLE_FMT_S16) {
        std::cerr << "Unsupported sample format: "
                  << av_get_sample_fmt_name(dec_ctx->sample_fmt) << '\n';
        return {};
    }

    // Tell ffmpeg to throw away everything that isnt the audio track, otherwise
    // it pulls the entire video through the demuxer just so we can skip it
    for (int i = 0; i < (int)pFormatContext->nb_streams; ++i)
        if (i != audio_stream_index)
            pFormatContext->streams[i]->discard = AVDISCARD_ALL;

    // 5.1 tracks keep the dialogue on the centre channel, so listen to that one
    // on its own when it is there - mixing it with the music and the effects
    // only buries the speech we are trying to find
    int channels = dec_ctx->ch_layout.nb_channels;
    int centre = av_channel_layout_index_from_channel(&dec_ctx->ch_layout, AV_CHAN_FRONT_CENTER);

    // 44100 does not divide into 8000, so we walk the input with a fractional
    // step and average everything that falls between two output samples. The
    // averaging doubles as a crude low pass. The old code took every 5th sample
    // which really gave 8820Hz and stretched the whole timeline by 9 percent
    double step = dec_ctx->sample_rate / 8000.0;
    double taken = 0;
    double sum = 0;
    int count = 0;

    auto take_frames = [&]() {
        while (true) {
            // Retrieves one decoded frame "EAGAIN" basically means not ready yet send more packages.
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                std::cerr << "Error while receiving a frame from the decoder" << '\n';
                break;
            }

            for (int i = 0; i < frame->nb_samples; ++i) {
                float s;
                if (centre >= 0) {
                    s = sample_at(frame, centre, i);
                } else {
                    s = 0;
                    for (int c = 0; c < channels; ++c) s += sample_at(frame, c, i);
                    s /= channels;
                }

                sum += s;
                count++;
                taken += 1.0;
                if (taken >= step) {
                    float avg = (float)(sum / count);
                    if (avg > 1.0f) avg = 1.0f;
                    if (avg < -1.0f) avg = -1.0f;
                    decoded.push_back((int16_t)(avg * 32767.0f));
                    sum = 0;
                    count = 0;
                    taken -= step;   // keep the leftover so the rate stays exact
                }
            }
            av_frame_unref(frame);
        }
    };

    // Read all the packets
    while (av_read_frame(pFormatContext, packet) >= 0) {
        // Filter so we only get audio streams
        if (packet->stream_index == audio_stream_index) {
            // Send the stuff to the decoder
            ret = avcodec_send_packet(dec_ctx, packet);
            if (ret < 0) {
                std::cerr << "Error while sending a packet to the decoder" << '\n';
                av_packet_unref(packet);
                break;
            }
            take_frames();
        }
        av_packet_unref(packet);   // without this every packet leaks its buffer
    }

    // Flush whatever the decoder is still sitting on, otherwise the last second
    // or so of the film never reaches us
    avcodec_send_packet(dec_ctx, NULL);
    take_frames();

    av_frame_free(&frame);
    av_packet_free(&packet);
    return decoded;
}

std::vector<float> calculate_fvad(const std::vector<int16_t>& pcm, int sample_rate) {
    std::vector<float> result = {};

    Fvad *vad = fvad_new();
    // Mode 0 called almost everything speech, music included. 2 is the middle
    // ground - the short gaps it leaves get glued back together in reference_spans
    fvad_set_mode(vad, 2);
    fvad_set_sample_rate(vad, sample_rate);

    int frame_size = sample_rate / 100; // 10ms
    for (int i = 0; i + frame_size <= (int)pcm.size(); i += frame_size) {
        int r = fvad_process(vad, pcm.data() + i, frame_size);
        result.push_back(r == 1 ? 1.0f : 0.0f);
    }

    fvad_free(vad);
    return result;
}


// use fvad instead i think
/*
std::vector<float> calculate_FFT(const std::vector<float>& decoded, int sample_rate) {
    fftw_import_wisdom_from_filename("wisdom.fftw");

    // FFTW standard is double perhaps needs to change to some of the fftw_ variants...
    std::vector<double> chunk = {};
    std::vector<float> FFT = {};
    int window_size = sample_rate / 100;
    float sum = 0;
    int N = decoded.size();
    int k_start = 300  * window_size / sample_rate;
    int k_end   = 3400 * window_size / sample_rate;
    int out_n = (window_size/2) + 1;

    fftw_complex *out;
    out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * out_n);

    for (int i = 0; i < N; ++i) {
        chunk.push_back(decoded[i]);

        if (i % window_size == window_size - 1) {
            fftw_plan p = fftw_plan_dft_r2c_1d(window_size, chunk.data(), out, FFTW_ESTIMATE);
            fftw_execute(p);

            for (int k = k_start; k <= k_end; k++) {
                double amplitude = std::sqrt(out[k][0] * out[k][0] + out[k][1] * out[k][1]);
                sum += amplitude;
            }
            FFT.push_back(sum);
            sum = 0;
            chunk.clear();
            fftw_destroy_plan(p);
        }
    }
    fftw_export_wisdom_to_filename("wisdom.fftw");
    fftw_free(out);
    return FFT;
}

// This under was another idea didnt work in my tests trying FFTW3 instead think thats better

// Calculate the RMS (Root mean square) and find the sound volume in 10 ms windows.
// The sample rate is 44100 so 44100/100 = 441 samples/10ms
// Had to cahnge to window size idk think its cause it dosnt match the activity profiles timescale so just trying this.

std::vector<float> calculate_RMS(const std::vector<float>& decoded, int sample_rate) {
    std::vector<float> RMS = {};
    int window_size = sample_rate / 100;
    float sum = 0;

    // RMS = sqrt(( x1^2+x2^2...xn^2) / n)
    for (int i = 0;i < decoded.size(); ++i) {
        float j = decoded[i];
        j *= j;
        sum += j;

        if (i % window_size == window_size - 1) {
            sum /= window_size;
            sum = sqrt(sum);
            //std::cout << "Sum: " << sum << '\n';
            RMS.push_back(sum);
            sum = 0;
        }
    }
    return RMS;
}
*/
