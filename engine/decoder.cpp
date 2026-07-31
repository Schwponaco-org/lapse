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

// Find the audio by looping though the streams from pFormatContext then checking by matching with the media type
int find_audio_stream(const AVFormatContext* pFormatContext) {
    for(int i {0}; i < pFormatContext->nb_streams; ++i) {
        if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // std::cout << i << '\n';
            return i;
        }

    }    
    return -1;
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
    }
    // This to get value from AVCodecParameters to the AVCodecContext so avcodec_open2 can work with it
    avcodec_parameters_to_context(dec_ctx, pFormatContext->streams[audio_stream_index]->codecpar);

    int ret = avcodec_open2(dec_ctx, NULL, NULL);
    if (ret < 0) {
        std::cerr << "Cannot open audio decoder" << '\n';
        return nullptr;
    }
    return dec_ctx;
}

// Decode the audio! My idea with vector float is that every float is a PCM-sample and a vector will grow while it reads packages from the file 
std::vector<float> decode_audio(AVFormatContext* pFormatContext, AVCodecContext* dec_ctx ,int audio_stream_index) {
    int ret;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    std::vector<float> decoded = {};

    if (!packet || !frame) {
        std::cerr << "Could not allocate frame or packet" << '\n';
        return {};
    }

    if (dec_ctx->sample_fmt != AV_SAMPLE_FMT_FLTP) {
        std::cerr << "Unsupported sample format: " 
                  << av_get_sample_fmt_name(dec_ctx->sample_fmt) << '\n';
        return {};
    }

    // Read all the packets
    while (1) {
        // Reads the compressed package from  file and puts it in a packet. 
        // Returns negative when the file is done
        if ((ret = av_read_frame(pFormatContext, packet)) < 0) {
            return decoded;
        }
        // Filter so we only get audio streams
        if (packet->stream_index == audio_stream_index) {
            // Send the stuff to the decoder
            ret = avcodec_send_packet(dec_ctx, packet);
            if (ret < 0) {
                std::cerr << "Error while sending a packet to the decoder" << '\n';
                return decoded;
            }
            // This loop is so we can get the decoded frames. The loop is because i tested without and it didnt work so a packet can have multiple frames
            while (ret >= 0) {
                // Retrieves one decoded frame "EAGAIN" basically means not ready yet send more packages.
                ret = avcodec_receive_frame(dec_ctx, frame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error while receiving a frame from the decoder" << '\n';
                    return decoded;
                }
                // The line below basically is just telling it to treat these bytes as another type here its "the data in raw bytes in frame->data[0] is actually floats"
                // Or i hope so at least it works ))
                float* samples = reinterpret_cast<float*>(frame->data[0]);
                for (int i = 0;i < frame->nb_samples ; ++i) {
                    decoded.push_back(samples[i]);
                }
            }
        }
    }
    return decoded; 
}

std::vector<int16_t> convert_to_8kHz(std::vector<float>& decoded, AVCodecContext* dec_ctx) {
    int ratio = dec_ctx->sample_rate / 8000;
    std::vector<int16_t> output;
    output.reserve(decoded.size() / ratio);
    
    // Take every nth sample to go from original rate to 8kHz
    for (size_t i = 0; i < decoded.size(); i += ratio) {
        output.push_back((int16_t)(decoded[i] * 32767.0f));
    }
    return output;
}

std::vector<float> calculate_fvad(const std::vector<int16_t>& pcm, int sample_rate) {
    std::vector<float> result = {};

    Fvad *vad = fvad_new();
    fvad_set_mode(vad, 0);
    fvad_set_sample_rate(vad, sample_rate);

    int frame_size = sample_rate / 100; // 10ms
    for (int i = 0; i + frame_size <= (int)pcm.size(); i += frame_size) {
        int r = fvad_process(vad, pcm.data() + i, frame_size);
        result.push_back(r == 1 ? 1.0f : 0.0f);
    }

    fvad_free(vad);
    return result;
}


// This function below i just couldnt figure out how to make it work
//Used the one above instead think its almost as good just not perfect sound wise but functional  
// Pls help or i need to look at it later!!!
/*
// Takes decoded float samples and squishes them down to 8kHz int16
// libfvad only needs 8k since speech sits under 4kHz anyway
std::vector<int16_t> convert_to_8kHz(const std::vector<float>& decoded, AVCodecContext* dec_ctx) {
    SwrContext* swr_ctx = nullptr;

    // Build the mono layouts the safe way. The AV_CHANNEL_LAYOUT_MONO macro
    // makes a struct that swr_convert later rejects, so we let ffmpeg fill it
    AVChannelLayout mono_in;
    AVChannelLayout mono_out;
    av_channel_layout_from_mask(&mono_in, AV_CH_LAYOUT_MONO);
    av_channel_layout_from_mask(&mono_out, AV_CH_LAYOUT_MONO);

    // input is mono float at movie rate, output is mono int16 at 8kHz
    int ret = swr_alloc_set_opts2(
        &swr_ctx,
        &mono_out,
        AV_SAMPLE_FMT_S16,
        8000,
        &mono_in,
        AV_SAMPLE_FMT_FLT,
        dec_ctx->sample_rate,
        0,
        NULL
    );
    if (ret < 0 || !swr_ctx) {
        std::cerr << "Could not set up resampler" << '\n';
        return {};
    }

    if (swr_init(swr_ctx) < 0) {
        std::cerr << "Could not init resampler" << '\n';
        swr_free(&swr_ctx);
        return {};
    }

    // Figure out how many samples we get out and grab the memory up front
    // so we dont reallocate mid conversion
    int64_t out_count = (int64_t)decoded.size() * 8000 / dec_ctx->sample_rate + 256;
    std::vector<int16_t> output(out_count);

    const uint8_t* in_ptr  = (const uint8_t*)decoded.data();
    uint8_t*       out_ptr = (uint8_t*)output.data();

    std::cout << "in samples: " << decoded.size() << " out_count: " << out_count << " in_rate: " << dec_ctx->sample_rate << '\n';

    // do the actual resample in one go
    int samples_out = swr_convert(swr_ctx, &out_ptr, (int)out_count, &in_ptr, (int)decoded.size());
    if (samples_out < 0) {
        char errbuf[128];
        av_strerror(samples_out, errbuf, sizeof(errbuf));
        std::cerr << "Error during resampling: " << errbuf << '\n';
        swr_free(&swr_ctx);
        return {};
    }

    // flush whatever the resampler still has buffered internally
    out_ptr = (uint8_t*)(output.data() + samples_out);
    int flushed = swr_convert(swr_ctx, &out_ptr, (int)(out_count - samples_out), NULL, 0);

    av_channel_layout_uninit(&mono_in);
    av_channel_layout_uninit(&mono_out);
    swr_free(&swr_ctx);

    output.resize(samples_out + flushed);
    return output;
}

std::vector<float> calculate_fvad(const std::vector<int16_t>& pcm, int sample_rate) {
    std::vector<float> result = {};

    Fvad *vad = fvad_new();
    fvad_set_mode(vad, 0);
    fvad_set_sample_rate(vad, sample_rate);

    int frame_size = sample_rate / 100; // 10ms
    for (int i = 0; i + frame_size <= (int)pcm.size(); i += frame_size) {
        int r = fvad_process(vad, pcm.data() + i, frame_size);
        result.push_back(r == 1 ? 1.0f : 0.0f);
    }

    fvad_free(vad);
    return result;
}
*/


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