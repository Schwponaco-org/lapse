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

#include "decoder.h"
#include "log.h"
#include <chrono>
#include <thread>
#include <atomic>
#include "silero.h"
#include <algorithm>

static const int RATE = SILERO_RATE;
static const int FRAME_SAMPLES = RATE / 100;
static const int VAD_MODES = 4;
static const double TARGET_LEVEL = 0.05;

static const int LANES = 8;
static const int64_t PRIME_MS = 3000;
static const int64_t PROBE_MS = 180000;

static const int64_t PIECE_MS = 600000;

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

    say() << "Format: " << pFormatContext->iformat->name << " Duration: " << pFormatContext->duration << '\n';
    return pFormatContext;
}

// Plenty of files carry no frame rate worth having, or claim something silly
// like 1000 because they were muxed as variable rate. Time the packets instead
// and take the middle gap, which is the rate whatever plays this will land on
static double measure_fps(AVFormatContext* fmt, int stream) {
    std::vector<int64_t> stamps;
    AVPacket* packet = av_packet_alloc();
    if (!packet) return 0;

    while ((int)stamps.size() < 240 && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream && packet->pts != AV_NOPTS_VALUE)
            stamps.push_back(packet->pts);
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    if (stamps.size() < 30) return 0;

    // packets arrive in the order they decode, not the order they are shown
    std::sort(stamps.begin(), stamps.end());
    std::vector<int64_t> gaps;
    for (int i = 1; i < (int)stamps.size(); i++) gaps.push_back(stamps[i] - stamps[i - 1]);
    std::sort(gaps.begin(), gaps.end());

    int64_t middle = gaps[gaps.size() / 2];
    if (middle <= 0) return 0;
    double fps = 1.0 / (middle * av_q2d(fmt->streams[stream]->time_base));

    // mkv counts in whole milliseconds, so 29.97 comes back as 30.303. Land on
    // a rate somebody actually shot at when we are already nearly on one
    static const double rates[] = {23.976, 24, 25, 29.97, 30, 50, 60};
    for (double rate : rates)
        if (std::fabs(fps - rate) < rate * 0.02) return rate;
    return fps;
}

// MicroDVD subtitles count frames instead of time, so the video has to tell us
// how long a frame lasts. Zero when the file has no video in it
double probe_fps(const char* filename) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, filename, nullptr, nullptr) != 0) return 0;

    double fps = 0;
    if (avformat_find_stream_info(fmt, nullptr) >= 0) {
        int video = -1;
        for (int i = 0; i < (int)fmt->nb_streams; i++) {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { video = i; break; }
        }

        if (video >= 0) {
            AVRational rate = fmt->streams[video]->avg_frame_rate;
            if (rate.num <= 0 || rate.den <= 0) rate = fmt->streams[video]->r_frame_rate;
            if (rate.num > 0 && rate.den > 0) fps = av_q2d(rate);
            if (fps < 10 || fps > 120) fps = measure_fps(fmt, video);
        }
    }

    avformat_close_input(&fmt);
    return fps;
}

int find_audio_stream(const AVFormatContext* pFormatContext, int wanted) {
    int first = -1;
    int seen = 0;
    for(int i {0}; i < (int)pFormatContext->nb_streams; ++i) {
        if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (wanted >= 0) {
                if (seen == wanted) return i;
                seen++;
                continue;
            }
            if (first < 0) first = i;
            if (pFormatContext->streams[i]->disposition & AV_DISPOSITION_DEFAULT) return i;
        }

    }
    if (wanted >= 0) std::cerr << "There is no audio track " << wanted << " in this file\n";
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

static void rewind_file(AVFormatContext* fmt) {
    int64_t start = (fmt->start_time == AV_NOPTS_VALUE) ? 0 : fmt->start_time;
    if (av_seek_frame(fmt, -1, start, AVSEEK_FLAG_BACKWARD) < 0)
        av_seek_frame(fmt, -1, start, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
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
    rewind_file(fmt);

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

std::vector<std::pair<int, int>> embedded_spans(AVFormatContext* fmt, int wanted) {
    std::vector<int> candidates;
    int seen = 0;

    for (int i = 0; i < (int)fmt->nb_streams; ++i) {
        AVStream* stream = fmt->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
        if (!text_subtitle(stream->codecpar->codec_id)) continue;
        if (wanted >= 0) {
            if (seen++ == wanted) candidates.push_back(i);
            continue;
        }
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
            say() << "Using embedded subtitle track " << index << " with " << spans.size() << " cues\n";
            return spans;
        }
    }
    return {};
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

// how loud it gets when something is happening. the mean is no good, a film
// that is silent most of the time drags it to nothing
static double loud_level(const std::vector<int16_t>& pcm) {
    std::vector<float> rms;
    for (size_t at = 0; at + FRAME_SAMPLES <= pcm.size(); at += FRAME_SAMPLES) {
        double energy = 0;
        for (int i = 0; i < FRAME_SAMPLES; ++i) {
            double v = pcm[at + i] / 32768.0;
            energy += v * v;
        }
        rms.push_back((float)std::sqrt(energy / FRAME_SAMPLES));
    }
    if (rms.empty()) return TARGET_LEVEL;

    size_t at = rms.size() * 9 / 10;
    std::nth_element(rms.begin(), rms.begin() + at, rms.end());
    return rms[at];
}

static std::vector<float> analyse(const std::vector<int16_t>& pcm, bool silero) {
    std::vector<float> probability;
    std::vector<float> loudness;
    std::vector<float> block(FRAME_SAMPLES);

    for (size_t at = 0; at + FRAME_SAMPLES <= pcm.size(); at += FRAME_SAMPLES) {
        double energy = 0;
        for (int i = 0; i < FRAME_SAMPLES; ++i) {
            double v = pcm[at + i] / 32768.0;
            energy += v * v;
        }
        loudness.push_back((float)std::sqrt(energy / FRAME_SAMPLES));
    }
    if (loudness.empty()) return {};

    if (silero) {

        double boost = getenv("NOBOOST") ? 1.0 : TARGET_LEVEL * 2.0 / std::max(loud_level(pcm), 1e-5);
        if (boost < 1.0) boost = 1.0;
        if (boost > 12.0) boost = 12.0;

        int total = (int)(pcm.size() / SILERO_WINDOW);
        if (total < 8) return {};

        Lanes run;
        int lanes = (total >= LANES * 64) ? LANES : 1;
        lanes = silero_begin(run, lanes);
        if (!lanes) return {};

        int per = (total + lanes - 1) / lanes;
        std::vector<float> feed((size_t)lanes * SILERO_WINDOW);
        std::vector<float> got(lanes);
        std::vector<float> windows(total, 0.0f);

        for (int step = 0; step < per; ++step) {
            for (int l = 0; l < lanes; ++l) {
                float* row = feed.data() + (size_t)l * SILERO_WINDOW;
                int w = l * per + step;
                if (w >= total) {
                    std::fill(row, row + SILERO_WINDOW, 0.0f);
                    continue;
                }
                const int16_t* src = pcm.data() + (size_t)w * SILERO_WINDOW;
                for (int i = 0; i < SILERO_WINDOW; ++i) {
                    double v = src[i] / 32768.0 * boost;
                    if (v > 1.0) v = 1.0;
                    if (v < -1.0) v = -1.0;
                    row[i] = (float)v;
                }
            }
            if (!silero_step(run, feed.data(), got.data())) return {};
            for (int l = 0; l < lanes; ++l) {
                int w = l * per + step;
                if (w < total) windows[w] = got[l];
            }
        }

        // silero answers once per 32ms, everything else here works in 10ms
        probability.assign(loudness.size(), 0.0f);
        for (size_t i = 0; i < probability.size(); ++i) {
            size_t w = (i * 10 + 5) / 32;
            probability[i] = (w < windows.size()) ? windows[w] : 0.0f;
        }
        return probability;
    }

    Fvad* modes[VAD_MODES] = {nullptr, nullptr, nullptr, nullptr};
    for (int mode = 0; mode < VAD_MODES; ++mode) {
        modes[mode] = fvad_new();
        fvad_set_mode(modes[mode], mode);
        fvad_set_sample_rate(modes[mode], RATE);
    }

    double level = TARGET_LEVEL;
    size_t frame_no = 0;
    for (size_t at = 0; at + FRAME_SAMPLES <= pcm.size(); at += FRAME_SAMPLES, ++frame_no) {
        level = level * 0.995 + loudness[frame_no] * 0.005;
        double gain = TARGET_LEVEL / std::max(level, 1e-5);
        if (gain > 40.0) gain = 40.0;
        if (gain < 0.5) gain = 0.5;

        int16_t frame[FRAME_SAMPLES];
        for (int i = 0; i < FRAME_SAMPLES; ++i) {
            double v = pcm[at + i] / 32768.0 * gain;
            if (v > 1.0) v = 1.0;
            if (v < -1.0) v = -1.0;
            frame[i] = (int16_t)(v * 32767.0);
        }

        int votes = 0;
        for (int mode = 0; mode < VAD_MODES; ++mode)
            if (fvad_process(modes[mode], frame, FRAME_SAMPLES) == 1) votes++;
        probability.push_back(votes / (float)VAD_MODES);
    }

    for (int mode = 0; mode < VAD_MODES; ++mode)
        if (modes[mode]) fvad_free(modes[mode]);

    suppress_music(probability, loudness);
    return probability;
}


static SwrContext* open_mix(const AVCodecContext* dec, int kind, int centre, int left, int right) {
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    AVChannelLayout in = dec->ch_layout;
    if (in.nb_channels <= 0) return nullptr;

    SwrContext* swr = nullptr;
    if (swr_alloc_set_opts2(&swr, &mono, AV_SAMPLE_FMT_S16, RATE,
                            &in, dec->sample_fmt, dec->sample_rate, 0, nullptr) < 0)
        return nullptr;

    if (kind != 1) {
        std::vector<double> matrix(in.nb_channels, 0.0);
        if (kind == 0 && centre >= 0 && centre < in.nb_channels) {
            matrix[centre] = 1.0;
        } else if (kind == 2 && left >= 0 && right >= 0 && left < in.nb_channels && right < in.nb_channels) {
            matrix[left] = 0.5;
            matrix[right] = 0.5;
        } else {
            swr_free(&swr);
            return nullptr;
        }
        if (swr_set_matrix(swr, matrix.data(), in.nb_channels) < 0) {
            swr_free(&swr);
            return nullptr;
        }
    }

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        return nullptr;
    }
    return swr;
}

static void pour(SwrContext* swr, const AVFrame* frame, std::vector<int16_t>& into) {
    if (!swr) return;
    int have = frame ? frame->nb_samples : 0;
    int room = swr_get_out_samples(swr, have);
    if (room <= 0) return;

    size_t at = into.size();
    into.resize(at + room);
    uint8_t* dst = (uint8_t*)(into.data() + at);
    int got = swr_convert(swr, &dst, room, frame ? (const uint8_t**)frame->extended_data : nullptr, have);
    into.resize(at + (got > 0 ? got : 0));
}


static std::vector<int16_t> decode_range(const std::string& path, int stream_index, int mix,
                                         int centre, int left, int right,
                                         int64_t from_ms, int64_t to_ms) {
    std::vector<int16_t> pcm;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) return pcm;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return pcm; }
    if (stream_index < 0 || stream_index >= (int)fmt->nb_streams) { avformat_close_input(&fmt); return pcm; }

    AVCodecContext* dec = open_audio_decoder(fmt, stream_index);
    if (!dec) { avformat_close_input(&fmt); return pcm; }

    SwrContext* swr = open_mix(dec, mix, centre, left, right);
    if (!swr) { avcodec_free_context(&dec); avformat_close_input(&fmt); return pcm; }

    keep_only(fmt, stream_index);

    AVRational millis = {1, 1000};
    AVRational tb = fmt->streams[stream_index]->time_base;
    int base = container_start_ms(fmt);

    int64_t prime = from_ms > PRIME_MS ? from_ms - PRIME_MS : 0;
    if (from_ms > 0) {
        int64_t target = av_rescale_q(prime + base, millis, tb);
        av_seek_frame(fmt, stream_index, target, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);
    } else {
        rewind_file(fmt);
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::vector<int16_t> scratch;
    bool started = false;
    bool done = false;

    auto drain = [&]() {
        while (!done) {
            int ret = avcodec_receive_frame(dec, frame);
            if (ret < 0) break;

            int64_t pts = frame->best_effort_timestamp;
            double at = (pts == AV_NOPTS_VALUE) ? -1.0
                      : (double)av_rescale_q(pts, tb, millis) - base;

            if (at >= 0 && at >= (double)to_ms) { done = true; av_frame_unref(frame); break; }

            scratch.clear();
            pour(swr, frame, scratch);

            if (!scratch.empty()) {
                double head = (at >= 0) ? at : (double)from_ms;
                if (!started) {

                    long long skip = (long long)((from_ms - head) * RATE / 1000.0);
                    if (skip <= 0) {
                        pcm.assign((size_t)(-skip), 0);
                        pcm.insert(pcm.end(), scratch.begin(), scratch.end());
                        started = true;
                    } else if (skip < (long long)scratch.size()) {
                        pcm.insert(pcm.end(), scratch.begin() + skip, scratch.end());
                        started = true;
                    }
                } else {
                    pcm.insert(pcm.end(), scratch.begin(), scratch.end());
                }
            }
            av_frame_unref(frame);
        }
    };

    while (!done && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream_index) {
            if (avcodec_send_packet(dec, packet) >= 0) drain();
        }
        av_packet_unref(packet);
    }
    if (!done) {
        avcodec_send_packet(dec, nullptr);
        drain();
        scratch.clear();
        pour(swr, nullptr, scratch);
        if (started) pcm.insert(pcm.end(), scratch.begin(), scratch.end());
    }

    size_t want = (size_t)((to_ms - from_ms) * RATE / 1000);
    if (pcm.size() > want) pcm.resize(want);

    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return pcm;
}

std::vector<float> speech_profile(AVFormatContext* fmt, AVCodecContext* dec_ctx, int audio_stream_index, int windows, double* coverage) {
    (void)windows;
    bool silero = silero_open();
    if (coverage) *coverage = 1.0;

    int centre = av_channel_layout_index_from_channel(&dec_ctx->ch_layout, AV_CHAN_FRONT_CENTER);
    int left   = av_channel_layout_index_from_channel(&dec_ctx->ch_layout, AV_CHAN_FRONT_LEFT);
    int right  = av_channel_layout_index_from_channel(&dec_ctx->ch_layout, AV_CHAN_FRONT_RIGHT);

    std::vector<int> mixes;
    if (centre >= 0 && dec_ctx->ch_layout.nb_channels > 2) mixes.push_back(0);
    mixes.push_back(1);
    if (left >= 0 && right >= 0 && dec_ctx->ch_layout.nb_channels > 2) mixes.push_back(2);

    std::string path = fmt->url ? fmt->url : "";
    double duration_ms = (fmt->duration != AV_NOPTS_VALUE) ? fmt->duration / 1000.0 : 0.0;


    int winner = mixes[0];
    if (mixes.size() > 1) {
        int64_t probe_at = (int64_t)(duration_ms * 0.35);
        double best = -1;
        for (int mix : mixes) {
            std::vector<int16_t> part = decode_range(path, audio_stream_index, mix, centre, left, right,
                                                     probe_at, probe_at + PROBE_MS);
            std::vector<float> got = analyse(part, silero);
            if (got.empty() && silero) got = analyse(part, false);
            double score = profile_score(got);
            const char* name = (mix == 0) ? "centre" : (mix == 2 ? "front pair" : "downmix");
            say() << "Mix " << name << ": score=" << score << '\n';
            if (score > best) { best = score; winner = mix; }
        }
    }

    int frames = (int)(duration_ms / 10) + 1;
    std::vector<float> profile(frames, 0.0f);

    int pieces = (int)(((int64_t)duration_ms + PIECE_MS - 1) / PIECE_MS);
    if (pieces < 1) pieces = 1;

    int hands = (int)std::thread::hardware_concurrency();
    if (hands < 1) hands = 1;
    if (hands > 8) hands = 8;
    if (hands > pieces) hands = pieces;

    auto ends_at = [&](int j) {
        return (j == pieces - 1) ? (int64_t)duration_ms + 1000 : (j + 1) * PIECE_MS;
    };

    std::vector<std::vector<float>> parts(pieces);
    std::atomic<int> next_piece(0);
    std::atomic<int> finished(0);

    auto worker = [&]() {
        for (;;) {
            int j = next_piece++;
            if (j >= pieces) return;
            std::vector<int16_t> pcm = decode_range(path, audio_stream_index, winner,
                                                    centre, left, right, j * PIECE_MS, ends_at(j));
            parts[j] = analyse(pcm, silero);
            progress("Listening to the audio", ++finished, pieces);
        }
    };

    std::vector<std::thread> crew;
    for (int i = 1; i < hands; ++i) crew.emplace_back(worker);
    worker();
    for (auto& t : crew) t.join();
    progress_done();

    bool empty = false;
    for (auto& p : parts) if (p.empty()) empty = true;
    if (empty && silero) {
        say() << "A piece came back with nothing, running it again on libfvad\n";
        for (int j = 0; j < pieces; ++j) {
            if (!parts[j].empty()) continue;
            parts[j] = analyse(decode_range(path, audio_stream_index, winner, centre, left, right,
                                            j * PIECE_MS, ends_at(j)), false);
        }
    }

    for (int j = 0; j < pieces; ++j) {
        size_t at = (size_t)(j * PIECE_MS / 10);
        for (size_t i = 0; i < parts[j].size(); ++i) {
            size_t k = at + i;
            if (k < profile.size()) profile[k] = parts[j][i];
        }
    }
    return profile;
}
