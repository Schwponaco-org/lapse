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
#include "silero.h"
#include <algorithm>

static const int FRAME_SAMPLES = 80;
static const int VAD_MODES = 4;
static const double TARGET_LEVEL = 0.05;
static const int DECIDE_SAMPLES = 600 * 8000;

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
// Take the one ffmpeg marked as default if there is one, the first audio track is often a commentary or a dub
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

static void keep_only(AVFormatContext* fmt, int stream_index) {
    for (int i = 0; i < (int)fmt->nb_streams; ++i)
        fmt->streams[i]->discard = (i == stream_index) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
}

static int container_start_ms(const AVFormatContext* fmt) {
    if (fmt->start_time == AV_NOPTS_VALUE) return 0;
    return (int)(fmt->start_time / (AV_TIME_BASE / 1000));
}

static bool text_subtitle(AVCodecID id) {
    return id == AV_CODEC_ID_SUBRIP || id == AV_CODEC_ID_ASS || id == AV_CODEC_ID_SSA ||
           id == AV_CODEC_ID_WEBVTT || id == AV_CODEC_ID_MOV_TEXT || id == AV_CODEC_ID_TEXT ||
           id == AV_CODEC_ID_SUBVIEWER || id == AV_CODEC_ID_MICRODVD;
}

static std::vector<std::pair<int, int>> read_subtitle_stream(AVFormatContext* fmt, int index) {
    std::vector<std::pair<int, int>> spans;
    AVPacket* packet = av_packet_alloc();
    if (!packet) return spans;

    keep_only(fmt, index);
    av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);

    AVRational millis = {1, 1000};
    AVRational tb = fmt->streams[index]->time_base;
    int offset = container_start_ms(fmt);

    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == index && packet->pts != AV_NOPTS_VALUE && packet->duration > 0) {
            int start = (int)av_rescale_q(packet->pts, tb, millis) - offset;
            int length = (int)av_rescale_q(packet->duration, tb, millis);
            if (start >= 0 && length > 0 && length < 30000)
                spans.push_back({start, start + length});
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    std::sort(spans.begin(), spans.end());
    return spans;
}

std::vector<std::pair<int, int>> embedded_spans(AVFormatContext* fmt) {
    std::vector<int> candidates;

    for (int i = 0; i < (int)fmt->nb_streams; ++i) {
        AVStream* stream = fmt->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
        if (!text_subtitle(stream->codecpar->codec_id)) continue;
        if (stream->disposition & AV_DISPOSITION_FORCED) continue;
        if (stream->disposition & AV_DISPOSITION_HEARING_IMPAIRED) continue;
        candidates.push_back(i);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        int da = (fmt->streams[a]->disposition & AV_DISPOSITION_DEFAULT) ? 1 : 0;
        int db = (fmt->streams[b]->disposition & AV_DISPOSITION_DEFAULT) ? 1 : 0;
        return da > db;
    });

    for (int index : candidates) {
        std::vector<std::pair<int, int>> spans = read_subtitle_stream(fmt, index);
        if (spans.size() >= 50) {
            std::cout << "Using embedded subtitle track " << index << " with " << spans.size() << " cues\n";
            return spans;
        }
    }
    return {};
}

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

static void suppress_music(std::vector<float>& probability, const std::vector<float>& level) {
    int n = (int)level.size();
    if (n < 200) return;

    std::vector<double> sum(n + 1, 0.0);
    std::vector<double> sum_sq(n + 1, 0.0);
    for (int i = 0; i < n; ++i) {
        sum[i + 1] = sum[i] + level[i];
        sum_sq[i + 1] = sum_sq[i] + (double)level[i] * level[i];
    }

    int half = 50;
    for (int i = 0; i < n; ++i) {
        int lo = std::max(0, i - half);
        int hi = std::min(n, i + half + 1);
        int count = hi - lo;

        double mean = (sum[hi] - sum[lo]) / count;
        double var = (sum_sq[hi] - sum_sq[lo]) / count - mean * mean;
        if (var < 0) var = 0;
        if (mean < 1e-6) continue;

        double spread = std::sqrt(var) / mean;
        if (spread < 0.35)
            probability[i] *= (float)(spread / 0.35);
    }
}

static double profile_score(const std::vector<float>& probability) {
    if (probability.size() < 600) return 0.0;

    double total = 0;
    double sure = 0;
    for (float p : probability) {
        total += p;
        sure += std::max(p, 1.0f - p);
    }

    double ratio = total / probability.size();
    double decisive = sure / probability.size();

    double plausible = 1.0;
    if (ratio < 0.15) plausible = ratio / 0.15;
    else if (ratio > 0.65) plausible = std::max(0.0, (1.0 - ratio) / 0.35);

    return decisive * plausible;
}

static std::vector<float> analyse(const std::vector<int16_t>& pcm, bool silero) {
    std::vector<float> probability;
    std::vector<float> loudness;
    std::vector<float> block(FRAME_SAMPLES);
    double level = TARGET_LEVEL;

    std::vector<float> windows;
    std::vector<float> silero_block;
    Fvad* modes[VAD_MODES] = {nullptr, nullptr, nullptr, nullptr};

    if (silero) {
        silero_reset();
        silero_block.reserve(SILERO_WINDOW);
    } else {
        for (int mode = 0; mode < VAD_MODES; ++mode) {
            modes[mode] = fvad_new();
            fvad_set_mode(modes[mode], mode);
            fvad_set_sample_rate(modes[mode], 8000);
        }
    }

    for (size_t at = 0; at + FRAME_SAMPLES <= pcm.size(); at += FRAME_SAMPLES) {
        double energy = 0;
        for (int i = 0; i < FRAME_SAMPLES; ++i) {
            block[i] = pcm[at + i] / 32768.0f;
            energy += (double)block[i] * block[i];
        }
        float rms = (float)std::sqrt(energy / FRAME_SAMPLES);
        loudness.push_back(rms);

        level = level * 0.995 + rms * 0.005;
        double gain = TARGET_LEVEL / std::max(level, 1e-5);
        if (gain > 40.0) gain = 40.0;
        if (gain < 0.5) gain = 0.5;

        for (int i = 0; i < FRAME_SAMPLES; ++i) {
            double v = block[i] * gain;
            if (v > 1.0) v = 1.0;
            if (v < -1.0) v = -1.0;
            block[i] = (float)v;
        }

        if (silero) {
            for (int i = 0; i < FRAME_SAMPLES; ++i) {
                silero_block.push_back(block[i]);
                if ((int)silero_block.size() == SILERO_WINDOW) {
                    float p = silero_run(silero_block.data(), SILERO_WINDOW);
                    silero_block.clear();
                    if (p < 0) return {};
                    windows.push_back(p);
                }
            }
        } else {
            int16_t frame[FRAME_SAMPLES];
            for (int i = 0; i < FRAME_SAMPLES; ++i) frame[i] = (int16_t)(block[i] * 32767.0);
            int votes = 0;
            for (int mode = 0; mode < VAD_MODES; ++mode)
                if (fvad_process(modes[mode], frame, FRAME_SAMPLES) == 1) votes++;
            probability.push_back(votes / (float)VAD_MODES);
        }
    }

    for (int mode = 0; mode < VAD_MODES; ++mode)
        if (modes[mode]) fvad_free(modes[mode]);

    if (!silero) {
        suppress_music(probability, loudness);
        return probability;
    }

    // Silero answers once per 32ms, the rest of the engine works in 10ms steps
    probability.resize(loudness.size(), 0.0f);
    for (size_t i = 0; i < probability.size(); ++i) {
        size_t w = (i * 10 + 5) / 32;
        probability[i] = (w < windows.size()) ? windows[w] : 0.0f;
    }
    return probability;
}

std::vector<float> speech_profile(AVFormatContext* fmt, AVCodecContext* dec_ctx, int audio_stream_index) {
    if (dec_ctx->sample_fmt != AV_SAMPLE_FMT_FLTP && dec_ctx->sample_fmt != AV_SAMPLE_FMT_FLT &&
        dec_ctx->sample_fmt != AV_SAMPLE_FMT_S16P && dec_ctx->sample_fmt != AV_SAMPLE_FMT_S16) {
        std::cerr << "Unsupported sample format: "
                  << av_get_sample_fmt_name(dec_ctx->sample_fmt) << '\n';
        return {};
    }

    bool silero = silero_open();

    int centre = av_channel_layout_index_from_channel(&dec_ctx->ch_layout, AV_CHAN_FRONT_CENTER);
    int left   = av_channel_layout_index_from_channel(&dec_ctx->ch_layout, AV_CHAN_FRONT_LEFT);
    int right  = av_channel_layout_index_from_channel(&dec_ctx->ch_layout, AV_CHAN_FRONT_RIGHT);

    std::vector<int> mixes;
    if (centre >= 0) mixes.push_back(0);
    mixes.push_back(1);
    if (left >= 0 && right >= 0 && dec_ctx->ch_layout.nb_channels > 2) mixes.push_back(2);

    int n = (int)mixes.size();
    std::vector<std::vector<int16_t>> keep(n);
    std::vector<double> sum(n, 0.0);
    int winner = (n == 1) ? 0 : -1;

    double step = dec_ctx->sample_rate / 8000.0;
    double taken = 0;
    int count = 0;

    keep_only(fmt, audio_stream_index);
    av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!packet || !frame) {
        std::cerr << "Could not allocate frame or packet" << '\n';
        return {};
    }

    int lead_ms = -1;
    AVRational millis = {1, 1000};
    AVRational tb = fmt->streams[audio_stream_index]->time_base;

    auto pick_winner = [&]() {
        double best_score = -1;
        for (int m = 0; m < n; ++m) {
            std::vector<float> profile = analyse(keep[m], silero);
            if (profile.empty() && silero) {
                silero = false;
                profile = analyse(keep[m], false);
            }
            double score = profile_score(profile);
            const char* name = (mixes[m] == 0) ? "centre" : (mixes[m] == 2 ? "front pair" : "downmix");
            std::cout << "Mix " << name << ": score=" << score << '\n';
            if (score > best_score) {
                best_score = score;
                winner = m;
            }
        }
        for (int m = 0; m < n; ++m)
            if (m != winner) std::vector<int16_t>().swap(keep[m]);
    };

    auto take_frames = [&]() {
        while (true) {
            int ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                std::cerr << "Error while receiving a frame from the decoder" << '\n';
                break;
            }

            int channels = frame->ch_layout.nb_channels;
            for (int i = 0; i < frame->nb_samples; ++i) {
                for (int m = 0; m < n; ++m) {
                    if (winner >= 0 && m != winner) continue;
                    float v = 0;
                    if (mixes[m] == 0) {
                        v = sample_at(frame, centre, i);
                    } else if (mixes[m] == 2) {
                        v = (sample_at(frame, left, i) + sample_at(frame, right, i)) * 0.5f;
                    } else {
                        for (int c = 0; c < channels; ++c) v += sample_at(frame, c, i);
                        v /= channels;
                    }
                    sum[m] += v;
                }

                count++;
                taken += 1.0;
                if (taken >= step) {
                    for (int m = 0; m < n; ++m) {
                        if (winner >= 0 && m != winner) continue;
                        double avg = sum[m] / count;
                        if (avg > 1.0) avg = 1.0;
                        if (avg < -1.0) avg = -1.0;
                        keep[m].push_back((int16_t)(avg * 32767.0));
                        sum[m] = 0;
                    }
                    count = 0;
                    taken -= step;

                    if (winner < 0 && (int)keep[0].size() >= DECIDE_SAMPLES) pick_winner();
                }
            }
            av_frame_unref(frame);
        }
    };

    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == audio_stream_index) {
            if (lead_ms < 0 && packet->pts != AV_NOPTS_VALUE) {
                lead_ms = (int)av_rescale_q(packet->pts, tb, millis) - container_start_ms(fmt);
                if (lead_ms < 0 || lead_ms > 60000) lead_ms = 0;
            }
            if (avcodec_send_packet(dec_ctx, packet) < 0) {
                std::cerr << "Error while sending a packet to the decoder" << '\n';
                av_packet_unref(packet);
                break;
            }
            take_frames();
        }
        av_packet_unref(packet);
    }

    avcodec_send_packet(dec_ctx, NULL);
    take_frames();

    av_frame_free(&frame);
    av_packet_free(&packet);

    if (winner < 0) pick_winner();

    std::vector<float> profile = analyse(keep[winner], silero);
    if (profile.empty() && silero) {
        silero = false;
        profile = analyse(keep[winner], false);
    }

    silero_close();

    if (lead_ms > 0)
        profile.insert(profile.begin(), lead_ms / 10, 0.0f);

    return profile;
}
