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

#include "correlate.h"

// slope og intercept for y = slope*x + intercept
std::pair<double, double> linear_regression(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& w) {
    double sw = 0, swx = 0 , swy = 0, swxx = 0, swxy = 0;
    for (int i = 0; i < (int)x.size(); i++) {
        sw   += w[i];
        swx  += w[i] * x[i];
        swy  += w[i] * y[i];
        swxx += w[i] * x[i] * x[i];
        swxy += w[i] * x[i] * y[i];
    }

    // One usable chunk cannot hold a line - fall back to a flat offset instead
    // of dividing by zero and writing NaN into every timestamp
    double denom = sw*swxx - swx*swx;
    if (sw <= 0) return {0.0, 0.0};
    if (std::abs(denom) < 1e-9) return {0.0, swy / sw};

    double slope = (sw*swxy - swx*swy) / denom;
    double intercept = (swy - slope*swx) / sw;
    return {slope, intercept};
}

std::pair<double, double> fft_crosscorrelate(const std::vector<int>& activity_profile, const std::vector<int>& srt_profile) {
    int padded = 262144;
    int chunk_size = 90000;
    int half = padded / 2 + 1;
    int max_lag = MAX_OFFSET_MS / 10;   // profiles hold one entry per 10ms

    std::vector<double> t_vals, delta_vals, weights;
    int chunk_number = 0;


    double* activity_buf = (double*) fftw_malloc(sizeof(double) * padded);
    double* srt_buf      = (double*) fftw_malloc(sizeof(double) * padded);
    fftw_complex* activity_fft = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * half);
    fftw_complex* srt_fft      = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * half);
    fftw_complex* corr_buf     = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * half);
    double* corr_out           = (double*) fftw_malloc(sizeof(double) * padded);


    fftw_plan plan_activity = fftw_plan_dft_r2c_1d(padded, activity_buf, activity_fft, FFTW_ESTIMATE);
    fftw_plan plan_srt      = fftw_plan_dft_r2c_1d(padded, srt_buf,      srt_fft,      FFTW_ESTIMATE);
    fftw_plan plan_inv      = fftw_plan_dft_c2r_1d(padded, corr_buf,     corr_out,     FFTW_ESTIMATE);



    for (int chunks = 45000; chunks + chunk_size <= (int)activity_profile.size(); chunks += chunk_size) {
        chunk_number++;

        // Both profiles are just ones and zeros so they carry a big constant
        // term. Taking the mean out first stops the correlation from mostly
        // measuring how much talking there is and gives a peak worth finding
        double a_mean = 0, b_mean = 0;
        for (int i = 0; i < chunk_size; ++i) {
            a_mean += activity_profile[chunks + i];
            if (chunks + i < (int)srt_profile.size()) b_mean += srt_profile[chunks + i];
        }
        a_mean /= chunk_size;
        b_mean /= chunk_size;

        // Fill activity buffer then full the rest with zeros
        for (int i = 0; i < padded; ++i)
            activity_buf[i] = (i < chunk_size) ? activity_profile[chunks + i] - a_mean : 0.0;

        // Fill srt buffer same window then zeros
        for (int i = 0; i < padded; ++i)
            srt_buf[i] = (i < chunk_size && chunks + i < (int)srt_profile.size()) ? srt_profile[chunks + i] - b_mean : 0.0;


        fftw_execute(plan_activity);
        fftw_execute(plan_srt);

        // Elementwise multiply activity with conjugate of srt
        for (int k = 0; k < half; ++k) {
            double a_re = activity_fft[k][0];
            double a_im = activity_fft[k][1];
            double b_re = srt_fft[k][0];
            double b_im = srt_fft[k][1];
            corr_buf[k][0] = a_re * b_re + a_im * b_im; // real
            corr_buf[k][1] = a_im * b_re - a_re * b_im; // imag
        }

        // Inverse FFT
        fftw_execute(plan_inv);

        // Normalize
        for (int i = 0; i < padded; ++i)
            corr_out[i] /= padded;

        // Find the peak, only looking at lags we would actually believe
        double best_val = -std::numeric_limits<double>::infinity();
        int best_lag = 0;

        for (int i = 0; i < padded; ++i) {
            int lag = (i < padded / 2) ? i : i - padded;
            if (std::abs(lag) > max_lag) continue;
            if (corr_out[i] > best_val) {
                best_val = corr_out[i];
                best_lag = lag;
            }
        }

        // The runner up has to come from somewhere else entirely. Taking the
        // bin next to the peak told us nothing - it is always nearly as high,
        // so every chunk came out with a sharpness of about 1
        double second_val = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < padded; ++i) {
            int lag = (i < padded / 2) ? i : i - padded;
            if (std::abs(lag) > max_lag) continue;
            if (std::abs(lag - best_lag) < 100) continue;   // skip a second either side
            if (corr_out[i] > second_val) second_val = corr_out[i];
        }

        double offset_ms = best_lag * 10.0;
        double sharpness = (second_val > 0) ? best_val / second_val : 0.0;

        std::cout << "t_" << chunk_number << " offset: " << offset_ms << "ms\n";
        std::cout << "Sharpness_" << chunk_number << ": " << sharpness << '\n';

        // A chunk that did not really lock onto anything only drags the line
        // around, so leave it out instead of weighting it
        if (sharpness < 1.05) continue;

        // Time of the middle of this chunk in seconds. The chunks start seven
        // and a half minutes in, which the old chunk_number*900 forgot about
        t_vals.push_back((chunks + chunk_size / 2) * 0.01);
        delta_vals.push_back(offset_ms / 1000.0);
        weights.push_back(sharpness);
    }

    // Cleanup everything afteruse like a good boy ))
    fftw_destroy_plan(plan_activity);
    fftw_destroy_plan(plan_srt);
    fftw_destroy_plan(plan_inv);
    fftw_free(activity_buf);
    fftw_free(srt_buf);
    fftw_free(activity_fft);
    fftw_free(srt_fft);
    fftw_free(corr_buf);
    fftw_free(corr_out);

    auto [slope, intercept] = linear_regression(t_vals, delta_vals, weights);
    std::cout << "Slope: " << slope << '\n';
    std::cout << "Intercept: " << intercept << '\n';

    return {slope, intercept};
}


/*
Needs to takeVAD spans
input spans from read_srt
One offset in ms
weight function pr span par
*/

double score_calculator(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, int x) {
    double score = 0;
    int n = 0;
    int k = 0;

    while (k < (int)reference_spans.size() && n < (int)read_srt.size()) {
        int overlap = std::max(0, std::min(reference_spans[k].second, read_srt[n].second + x) - std::max(reference_spans[k].first, read_srt[n].first + x));
        int min_length = std::min(reference_spans[k].second - reference_spans[k].first, read_srt[n].second - read_srt[n].first);
        int max_length = std::max(reference_spans[k].second - reference_spans[k].first, read_srt[n].second - read_srt[n].first);

        if (min_length > 0) {
            double iscore = (double)overlap / min_length;
            double w = (double)min_length / max_length;
            score += iscore * w;
        }

        if (reference_spans[k].second < read_srt[n].second + x)
            k += 1;
        else
            n +=1;
    }
    return score;
}

// Slides the subtitle spans over the reference and returns the offset where
// they overlap best, together with how much of the file that offset explains
std::pair<int, double> best_offset(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans) {
    std::vector<std::pair<int, float>> slope_changes;

    for (int k = 0; k < (int)reference_spans.size(); k++){
        for (int n = 0; n < (int)read_srt.size(); n++) {
            // This pair can only ever meet somewhere between these two offsets,
            // so skip it when that whole stretch is further out than we search
            int reach_lo = reference_spans[k].first - read_srt[n].second;
            int reach_hi = reference_spans[k].second - read_srt[n].first;
            if (reach_hi < -MAX_OFFSET_MS || reach_lo > MAX_OFFSET_MS) continue;

            int min_length = std::min(reference_spans[k].second - reference_spans[k].first, read_srt[n].second - read_srt[n].first);
            int max_length = std::max(reference_spans[k].second - reference_spans[k].first, read_srt[n].second - read_srt[n].first);
            if (min_length <= 0) continue;

            float w = (float)min_length / max_length;
            float slope = w / (float)min_length;

            int sig1 = reference_spans[k].first - read_srt[n].second;
            if (max_length == reference_spans[k].second - reference_spans[k].first) {
                int sig2 = reference_spans[k].first - read_srt[n].first;
                int sig3 = reference_spans[k].second - read_srt[n].second;

                slope_changes.push_back({sig2, -slope});
                slope_changes.push_back({sig3, -slope});
            } else {
                int sig2 = reference_spans[k].second - read_srt[n].second;
                int sig3 = reference_spans[k].first - read_srt[n].first;

                slope_changes.push_back({sig2, -slope});
                slope_changes.push_back({sig3, -slope});
            }
            int sig4 = reference_spans[k].second - read_srt[n].first;
            slope_changes.push_back({sig1, +slope});
            slope_changes.push_back({sig4, +slope});
        }
    }

    std::sort(slope_changes.begin(), slope_changes.end());

    // These have to be doubles. The slopes are fractions of a millisecond and
    // adding them to a long threw every one of them away, so the score never
    // moved off zero and the answer was always an offset of 0
    int  sig_last = 0;
    double fvalue = 0;
    double fmax = 0;
    double current_slope = 0;
    int best = 0;
    for (int i = 0; i < (int)slope_changes.size(); i++) {
        fvalue += current_slope * (slope_changes[i].first - sig_last);
        if (fvalue > fmax && std::abs(slope_changes[i].first) <= MAX_OFFSET_MS) {
            fmax = fvalue;
            best = slope_changes[i].first;
        }
        sig_last = slope_changes[i].first;
        current_slope += slope_changes[i].second;
    }

    // A cue that lands right on top of a reference span is worth 1, so this
    // reads as the share of the subtitle file that ended up on speech
    double confidence = read_srt.empty() ? 0.0 : fmax / read_srt.size();
    if (confidence > 1.0) confidence = 1.0;

    return {best, confidence};
}

// Stretches the subtitle by each framerate ratio in turn and keeps whichever
// one lines up best. Measuring the drift chunk by chunk never really worked 
// a quarter of an hour of film drifts more than half a minute at 4 percent, so
// there is no single lag that fits the chunk and the peak just smears out
std::tuple<double, int, double> best_framerate(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans) {
    double best_ratio = 1.0;
    int best_shift = 0;
    double best_confidence = -1;

    for (double ratio : FRAMERATE_RATIOS) {
        std::vector<std::pair<int, int>> scaled;
        scaled.reserve(read_srt.size());
        for (auto& s : read_srt)
            scaled.push_back({(int)(s.first * ratio), (int)(s.second * ratio)});

        auto [offset, confidence] = best_offset(scaled, reference_spans);
        std::cout << "ratio " << ratio << ": offset=" << offset << "ms confidence=" << confidence << '\n';

        if (confidence > best_confidence) {
            best_confidence = confidence;
            best_ratio = ratio;
            best_shift = offset;
        }
    }
    return {best_ratio, best_shift, best_confidence};
}

//  Same idea as best_offset but for a single span, and instead of the best
// offset we keep the whole score for every offset in the window. Sampling it
// every 10ms rather than every millisecond is what makes split mode fit in
//memory the old per millisecond version needed gigabytes for a film
std::vector<double> score_curve(const std::pair<int,int>& span, const std::vector<std::pair<int,int>>& reference_spans, int lo, int hi, int step) {
    std::vector<std::pair<int, float>> slope_changes;
    std::vector<double> curve;

    for (int k = 0; k < (int)reference_spans.size(); k++) {
        int min_length = std::min(reference_spans[k].second - reference_spans[k].first, span.second - span.first);
        int max_length = std::max(reference_spans[k].second - reference_spans[k].first, span.second - span.first);
        if (min_length <= 0) continue;

        float w = (float)min_length / max_length;
        float slope = w / (float)min_length;

        int sig1 = reference_spans[k].first - span.second;
        if (max_length == reference_spans[k].second - reference_spans[k].first) {
            int sig2 = reference_spans[k].first - span.first;
            int sig3 = reference_spans[k].second - span.second;

            slope_changes.push_back({sig2, -slope});
            slope_changes.push_back({sig3, -slope});
        } else {
            int sig2 = reference_spans[k].second - span.second;
            int sig3 = reference_spans[k].first - span.first;

            slope_changes.push_back({sig2, -slope});
            slope_changes.push_back({sig3, -slope});
        }
        int sig4 = reference_spans[k].second - span.first;
        slope_changes.push_back({sig1, +slope});
        slope_changes.push_back({sig4, +slope});
    }

    std::sort(slope_changes.begin(), slope_changes.end());

    curve.reserve((hi - lo) / step + 1);
    double fvalue = 0;
    double current_slope = 0;
    int sig_last = slope_changes.empty() ? lo : slope_changes[0].first;
    size_t j = 0;

    for (int offset = lo; offset <= hi; offset += step) {
        while (j < slope_changes.size() && slope_changes[j].first <= offset) {
            fvalue += current_slope * (slope_changes[j].first - sig_last);
            sig_last = slope_changes[j].first;
            current_slope += slope_changes[j].second;
            j++;
        }
        fvalue += current_slope * (offset - sig_last);
        sig_last = offset;
        curve.push_back(fvalue);
    }

    return curve;
}

std::vector<int> split_alignment(const std::vector<std::pair<int,int>>& read_srt, const std::vector<std::pair<int,int>>& reference_spans, float p, int base_offset) {
    // Every span is scored on the same grid of offsets around the one nosplit
    // already found. Sharing the grid is what makes the indexes comparable
    // from one span to the next each span having its own base was why the offsets came out wrong before
    int lo = base_offset - SPLIT_WINDOW_MS;
    int hi = base_offset + SPLIT_WINDOW_MS;

    std::vector<double> t_prev = score_curve(read_srt[0], reference_spans, lo, hi, SPLIT_STEP_MS);
    std::vector<std::vector<int>> all_to;

    for (int n = 1; n < (int)read_srt.size(); n++) {
        std::vector<double> scores = score_curve(read_srt[n], reference_spans, lo, hi, SPLIT_STEP_MS);
        std::vector<double> t_new(scores.size(), 0);
        std::vector<int> to(scores.size(), 0);

        // Subtitles are not allowed to swap places. If this span sits at sigma
        // the one before it can sit anywhere up to sigma plus the gap between
        // them, so we walk a running best over everything allowed so far
        int gap = (read_srt[n].first - read_srt[n-1].second) / SPLIT_STEP_MS;
        double s_max = -std::numeric_limits<double>::infinity();
        int s_max_at = 0;
        int reached = -1;

        for (int sigma = 0; sigma < (int)scores.size(); sigma++) {
            int allowed = std::min(sigma + gap, (int)t_prev.size() - 1);
            while (reached < allowed) {
                reached++;
                if (t_prev[reached] > s_max) {
                    s_max = t_prev[reached];
                    s_max_at = reached;
                }
            }

            // Staying where the previous span sits is free, moving somewhere else costs the split penalty
            if (t_prev[sigma] >= s_max - p) {
                t_new[sigma] = scores[sigma] + t_prev[sigma];
                to[sigma] = sigma;
            } else {
                t_new[sigma] = scores[sigma] + s_max - p;
                to[sigma] = s_max_at;
            }
        }
        all_to.push_back(to);
        t_prev.swap(t_new);
    }

    int sigma_best = 0;
    for (int i = 1; i < (int)t_prev.size(); i++) {
        if (t_prev[i] > t_prev[sigma_best]) {
            sigma_best = i;
        }
    }

     // Walk the choices back and turn the grid positions into real offsets
    std::vector<int> offsets(read_srt.size());
    offsets[read_srt.size() - 1] = lo + sigma_best * SPLIT_STEP_MS;

    for (int n = all_to.size() - 1; n >= 0; n--) {
        sigma_best = all_to[n][sigma_best];
        offsets[n] = lo + sigma_best * SPLIT_STEP_MS;
    }
    return offsets;
}

/*
// Finds the offset between the movie and subtitles
std::tuple<double, double, float> cross_correlation(std::vector<int> activity_profile, std::vector<float> RMS) {
    // starts checking one minute behind
    std::vector<double> t_vals, delta_vals, weights;
    float best_sum = -std::numeric_limits<float>::infinity();
    float second_best_sum = -std::numeric_limits<float>::infinity();
    int initial_offset {};
    int offset_other {};
    float all_sums = 0;
    float all_sums_other = 0;
    int count = 0;
    int chunk_number = 0;
    long long activity = 0;
    int best_chunk_start = 0;
    long long best_activity = 0;

    for (int c = 45000; c + 90000 <= activity_profile.size(); c += 90000) {
        long long act = 0;
        for (int i = c; i < c + 90000; ++i)
            act += activity_profile[i];
        if (act > best_activity) {
            best_activity = act;
            best_chunk_start = c;
        }
    }

    for (int  chunks = 45000; chunks + 90000 <= activity_profile.size(); chunks += 90000) {
        chunk_number += 1;
        activity = 0;
        for (int i = chunks; i < chunks + 90000; ++i)
            activity += activity_profile[i];

        if (chunk_number == best_chunk_start) {
            for (int offset = -120000; offset <= 120000; offset += 10) {
                float sum = 0;
                int offset_index = offset / 10;

                // The idea is: slide the subtitle timeline over the audio energy profile, multiply
                // and sum at each offset. Loud audio + subtitle activity aligned = high score.
                // The offset with the highest score is the sync point.

                for (int i = 0; i < 90000; ++i) {
                    int rms_index = i - offset_index;
                    if (rms_index >= 0 && rms_index < RMS.size()) {
                    sum += activity_profile[i] * RMS[rms_index];
                    }
                }
                if (sum > best_sum) {
                    best_sum = sum;
                    initial_offset = offset;
                } else if (abs(offset - initial_offset) > 1000 && sum > second_best_sum) {
                    second_best_sum = sum;
                }
                all_sums += sum;
                count++;
                //if (offset <= -120000 + 200)
                //    std::cout << "offset: " << offset << " sum: " << sum << " best: " << best_sum << '\n';
            }
        float sharpness = best_sum / second_best_sum;
        std::cout << "t_" << chunk_number << " " << initial_offset << '\n';
        std::cout << "Activity: " << activity << '\n';
        std::cout << "Sharpness_" << chunk_number << ": " << sharpness << '\n';
        t_vals.push_back((chunk_number - 0.5) * 900);
        delta_vals.push_back(initial_offset / 1000.0);
        weights.push_back(sharpness);
        } else {
            best_sum = -std::numeric_limits<float>::infinity();
            second_best_sum = -std::numeric_limits<float>::infinity();
            offset_other = 0;

            for (int offset = -10000; offset <= 10000; offset += 10) {
                float sum = 0;
                int offset_index = offset / 10;

                for (int i = chunks; i < chunks + 90000; ++i) {
                    int rms_index = i - offset_index;
                    if (rms_index >= 0 && rms_index < RMS.size()) {
                    sum += activity_profile[i] * RMS[rms_index];
                    }
                }
                if (sum > best_sum) {
                    best_sum = sum;
                    offset_other = offset;
                } else if (abs(offset - offset_other) > 1000 && sum > second_best_sum) {
                    second_best_sum = sum;
                }
            }
        float sharpness = best_sum / second_best_sum;
        std::cout << "t_" << chunk_number << " " << offset_other << '\n';
        std::cout << "Activity_" << chunk_number << ": " << activity << '\n';
        std::cout << "Sharpness_" << chunk_number << ": " << sharpness << '\n';
        t_vals.push_back((chunk_number - 0.5) * 900);
        delta_vals.push_back(offset_other / 1000.0);
        weights.push_back(sharpness);
        }
    }

    auto [slope, intercept] = linear_regression(t_vals, delta_vals, weights);
    std::cout << "Slope: " << slope << '\n';
    std::cout << "Intercept: " << intercept << '\n';

    float confidence = best_sum / (all_sums / count);
    return {slope, intercept, confidence};
}
*/
