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

#include "correlate.h"
#include "align.h"
#include "log.h"
#include <cstdlib>

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
    int max_lag = MAX_OFFSET_MS / 10; // profiles hold one entry per 10ms

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

        double second_val = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < padded; ++i) {
            int lag = (i < padded / 2) ? i : i - padded;
            if (std::abs(lag) > max_lag) continue;
            if (std::abs(lag - best_lag) < 100) continue;   // skip a second either side
            if (corr_out[i] > second_val) second_val = corr_out[i];
        }


        int at = (best_lag >= 0) ? best_lag : best_lag + padded;
        double left  = corr_out[(at - 1 + padded) % padded];
        double right = corr_out[(at + 1) % padded];
        double curve = left - 2 * best_val + right;
        double shift = (std::abs(curve) > 1e-12) ? 0.5 * (left - right) / curve : 0.0;
        if (shift > 0.5) shift = 0.5;
        if (shift < -0.5) shift = -0.5;

        double offset_ms = (best_lag + shift) * 10.0;
        double sharpness = (second_val > 0) ? best_val / second_val : 0.0;

        say() << "t_" << chunk_number << " offset: " << offset_ms << "ms\n";
        say() << "Sharpness_" << chunk_number << ": " << sharpness << '\n';


        if (sharpness < 1.05) continue;


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
    say() << "Slope: " << slope << '\n';
    say() << "Intercept: " << intercept << '\n';

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


Lock best_offset(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, const std::vector<float>& reference_weights, double coverage, int max_offset) {

    if (align_ready() && max_offset <= align_reach() && !read_srt.empty()) {
        std::vector<Hit> tops = align_peaks(read_srt, 10);


        Hit stay;
        tops.push_back(stay);


        std::vector<std::pair<double, Hit>> ranked;
        for (auto& h : tops) {
            if (std::abs(h.offset) > max_offset) continue;
            ranked.push_back({onset_z(read_srt, h.offset), h});
        }
        std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first > b.first; });

        Hit best;
        double best_lock = -1;
        bool have = false;

        for (int i = 0; i < (int)ranked.size() && i < 4; i++) {
            Hit c = ranked[i].second;
            c.offset = align_refine(read_srt, c.offset, 1200);
            if (std::abs(c.offset) > max_offset) continue;
            c.score = align_score(read_srt, c.offset);
            double lock = onset_z(read_srt, c.offset);

            if (!have || lock > best_lock) {
                best_lock = lock;
                best = c;
                have = true;
            }
        }
        if (have) {
            if (coverage <= 0) coverage = 1.0;
            double confidence = best.score / (read_srt.size() * coverage);
            if (confidence > 1.0) confidence = 1.0;
            double margin = 1.0 - best.runner;
            if (margin < 0) margin = 0;
            if (best_lock < 0) best_lock = 0;
            return {best.offset, confidence, margin, best_lock};
        }
    }

    std::vector<std::pair<int, float>> slope_changes;
    bool weighted = reference_weights.size() == reference_spans.size();

    for (int k = 0; k < (int)reference_spans.size(); k++){
        for (int n = 0; n < (int)read_srt.size(); n++) {
            // This pair can only ever meet somewhere between these two offsets,
            // so skip it when that whole stretch is further out than we search
            int reach_lo = reference_spans[k].first - read_srt[n].second;
            int reach_hi = reference_spans[k].second - read_srt[n].first;
            if (reach_hi < -max_offset || reach_lo > max_offset) continue;

            int min_length = std::min(reference_spans[k].second - reference_spans[k].first, read_srt[n].second - read_srt[n].first);
            int max_length = std::max(reference_spans[k].second - reference_spans[k].first, read_srt[n].second - read_srt[n].first);
            if (min_length <= 0) continue;

            float w = (float)min_length / max_length;
            if (weighted) w *= reference_weights[k];
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

    const int bucket_ms = 1000;
    int buckets = (2 * max_offset) / bucket_ms + 1;
    std::vector<double> peak(buckets, 0.0);

    int  sig_last = 0;
    double fvalue = 0;
    double fmax = 0;
    double current_slope = 0;
    int best = 0;
    for (int i = 0; i < (int)slope_changes.size(); i++) {
        fvalue += current_slope * (slope_changes[i].first - sig_last);
        if (std::abs(slope_changes[i].first) <= max_offset) {
            if (fvalue > fmax) {
                fmax = fvalue;
                best = slope_changes[i].first;
            }
            int b = (slope_changes[i].first + max_offset) / bucket_ms;
            if (fvalue > peak[b]) peak[b] = fvalue;
        }
        sig_last = slope_changes[i].first;
        current_slope += slope_changes[i].second;
    }

    if (coverage <= 0) coverage = 1.0;
    double confidence = read_srt.empty() ? 0.0 : fmax / (read_srt.size() * coverage);
    if (confidence > 1.0) confidence = 1.0;

    int best_bucket = (best + max_offset) / bucket_ms;
    double rival = 0;
    for (int b = 0; b < buckets; b++)
        if (std::abs(b - best_bucket) > 1 && peak[b] > rival) rival = peak[b];

    double margin = (fmax > 0) ? (fmax - rival) / fmax : 0.0;
    if (margin < 0) margin = 0;

    return {best, confidence, margin, peak_sigma(peak, best_bucket, fmax)};
}

double peak_sigma(const std::vector<double>& peak, int best_bucket, double fmax) {
    if (fmax <= 0) return 0.0;

    std::vector<double> noise;
    noise.reserve(peak.size());
    for (int b = 0; b < (int)peak.size(); b++) {
        if (std::abs(b - best_bucket) <= 2) continue;   // the peak's own shoulders
        noise.push_back(peak[b]);
    }
    if (noise.size() < 8) return 0.0;

    std::sort(noise.begin(), noise.end());
    double middle = noise[noise.size() / 2];

    std::vector<double> away;
    away.reserve(noise.size());
    for (double v : noise) away.push_back(std::abs(v - middle));
    std::sort(away.begin(), away.end());
    double spread = 1.4826 * away[away.size() / 2];

    if (spread < 1e-9) {
        double sum = 0;
        for (double v : noise) sum += std::abs(v - middle);
        spread = sum / noise.size();
    }
    if (spread < 1e-9) return (fmax > middle) ? 99.0 : 0.0;

    double sigma = (fmax - middle) / spread;
    if (sigma < 0) sigma = 0;
    if (sigma > 99.0) sigma = 99.0;
    return sigma;
}

std::tuple<double, int, double, double> best_framerate(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, const std::vector<float>& reference_weights, double coverage) {
    double best_ratio = 1.0;
    int best_shift = 0;
    double best_confidence = 0;
    double best_sigma = -1;

    for (double ratio : FRAMERATE_RATIOS) {
        std::vector<std::pair<int, int>> scaled;
        scaled.reserve(read_srt.size());
        for (auto& s : read_srt)
            scaled.push_back({(int)(s.first * ratio), (int)(s.second * ratio)});

        Lock got = best_offset(scaled, reference_spans, reference_weights, coverage);
        say() << "ratio " << ratio << ": offset=" << got.offset << "ms confidence=" << got.confidence
              << " sigma=" << got.sigma << '\n';

        // picking on confidence handed it to whichever ratio smeared widest
        if (got.sigma > best_sigma) {
            best_sigma = got.sigma;
            best_confidence = got.confidence;
            best_ratio = ratio;
            best_shift = got.offset;
        }
    }
    return {best_ratio, best_shift, best_confidence, best_sigma};
}


std::vector<Chunk> chunk_offsets(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, const std::vector<float>& reference_weights, int count, double coverage, int max_offset) {
    std::vector<Chunk> chunks;
    if ((int)read_srt.size() < count * 5) count = (int)read_srt.size() / 5;
    if (count < 2) return chunks;

    int per = (int)read_srt.size() / count;

    for (int c = 0; c < count; c++) {
        int from = c * per;
        int to = (c == count - 1) ? (int)read_srt.size() : (c + 1) * per;

        std::vector<std::pair<int, int>> part(read_srt.begin() + from, read_srt.begin() + to);
        Lock got = best_offset(part, reference_spans, reference_weights, coverage, max_offset);

        double middle = 0;
        for (auto& s : part) middle += (s.first + s.second) / 2.0;
        middle /= part.size();

        chunks.push_back({middle, (double)got.offset, got.confidence * got.margin, got.sigma});
        progress("Checking the file in slices", c + 1, count);
    }
    progress_done();
    return chunks;
}

std::vector<int> backward_jumps(const std::vector<std::pair<int, int>>& read_srt) {
    std::vector<int> cuts;
    for (size_t i = 1; i < read_srt.size(); i++)
        if (read_srt[i].first < read_srt[i - 1].first - CONCAT_BACK_MS)
            cuts.push_back((int)i);
    return cuts;
}

std::vector<int> offsets_for_cuts(const std::vector<std::pair<int, int>>& read_srt,
    const std::vector<std::pair<int, int>>& reference_spans,
    const std::vector<float>& reference_weights,
    const std::vector<int>& cuts,
    double coverage,
    double* worst_sigma) {
    int n = (int)read_srt.size();
    std::vector<int> offsets(n, 0);
    double worst = 99.0;

    std::vector<int> edge;
    edge.push_back(0);
    for (int c : cuts) if (c > 0 && c < n) edge.push_back(c);
    edge.push_back(n);

    for (size_t e = 0; e + 1 < edge.size(); e++) {
        int from = edge[e], to = edge[e + 1];
        if (from >= to) continue;

        std::vector<std::pair<int, int>> part(read_srt.begin() + from, read_srt.begin() + to);
        Lock got = best_offset(part, reference_spans, reference_weights,
                                                        coverage, CONCAT_SEARCH_MS);
        say() << "part " << (e + 1) << ": cues " << from << "-" << to
                  << " offset=" << got.offset << "ms confidence=" << got.confidence
                  << " sigma=" << got.sigma << '\n';

        for (int k = from; k < to; k++) offsets[k] = got.offset;
        if (got.sigma < worst) worst = got.sigma;
    }

    if (worst_sigma) *worst_sigma = worst;
    return offsets;
}

std::vector<int> concat_offsets(const std::vector<std::pair<int, int>>& read_srt,
                                const std::vector<std::pair<int, int>>& reference_spans,
                                const std::vector<float>& reference_weights,
                                double coverage,
                                double* worst_sigma) {
    std::vector<Chunk> chunks = chunk_offsets(read_srt, reference_spans, reference_weights,
        16, coverage, CONCAT_SEARCH_MS);
    if (chunks.size() < 4) return {};

    std::vector<int> anchor;
    for (auto& c : chunks) {
        if (c.confidence < 0.02) continue;
        if (anchor.empty() || std::abs(c.offset - anchor.back()) > CONCAT_JUMP_MS)
            anchor.push_back((int)c.offset);
    }
    if (anchor.size() < 2) return {};

    int n = (int)read_srt.size();
    std::vector<int> cuts;
    int from = 0;

    for (size_t a = 0; a + 1 < anchor.size(); a++) {
        int lo = anchor[a], hi = anchor[a + 1];
        double best = -1;
        int cut = from;

        for (int j = from; j <= n; j++) {
            std::vector<std::pair<int, int>> left(read_srt.begin() + from, read_srt.begin() + j);
            std::vector<std::pair<int, int>> right(read_srt.begin() + j, read_srt.end());
            double sc = score_calculator(left, reference_spans, lo) + score_calculator(right, reference_spans, hi);
            if (sc > best) { best = sc; cut = j; }
        }
        if (cut > from && cut < n) cuts.push_back(cut);
        from = cut;
    }
    if (cuts.empty()) return {};

    return offsets_for_cuts(read_srt, reference_spans, reference_weights, cuts, coverage, worst_sigma);
}


std::pair<double, double> robust_line(const std::vector<Chunk>& chunks) {
    std::vector<double> slopes;
    for (size_t i = 0; i < chunks.size(); i++) {
        for (size_t j = i + 1; j < chunks.size(); j++) {
            double dt = chunks[j].time - chunks[i].time;
            if (std::abs(dt) < 1000) continue;
            slopes.push_back((chunks[j].offset - chunks[i].offset) / dt);
        }
    }
    if (slopes.empty()) return {0.0, 0.0};

    std::sort(slopes.begin(), slopes.end());
    double slope = slopes[slopes.size() / 2];

    std::vector<double> intercepts;
    for (auto& c : chunks) intercepts.push_back(c.offset - slope * c.time);
    std::sort(intercepts.begin(), intercepts.end());

    return {slope, intercepts[intercepts.size() / 2]};
}


double snap_ratio(double ratio) {
    double best = ratio;
    double closest = 0.004;
    for (double candidate : FRAMERATE_RATIOS) {
        double away = std::abs(candidate - ratio);
        if (away < closest) {
            closest = away;
            best = candidate;
        }
    }
    return best;
}

//  Same idea as best_offset but for a single span, and instead of the best
// offset we keep the whole score for every offset in the window. Sampling it
// every 10ms rather than every millisecond is what makes split mode fit in
//memory the old per millisecond version needed gigabytes for a film
std::vector<double> score_curve(const std::pair<int,int>& span, const std::vector<std::pair<int,int>>& reference_spans, const std::vector<float>& reference_weights, int lo, int hi, int step) {
    std::vector<std::pair<int, float>> slope_changes;
    std::vector<double> curve;
    bool weighted = reference_weights.size() == reference_spans.size();

    // only the spans this cue could ever reach over the window we are drawing. walking all of them cost more than the whole rest of split mode
    int first = (int)(std::lower_bound(reference_spans.begin(), reference_spans.end(),
        std::make_pair(span.first + lo, span.first + lo),
        [](const std::pair<int,int>& a, const std::pair<int,int>& b) { return a.second < b.second; }) - reference_spans.begin());

    for (int k = first; k < (int)reference_spans.size(); k++) {
        if (reference_spans[k].first > span.second + hi) break;
        int min_length = std::min(reference_spans[k].second - reference_spans[k].first, span.second - span.first);
        int max_length = std::max(reference_spans[k].second - reference_spans[k].first, span.second - span.first);
        if (min_length <= 0) continue;

        float w = (float)min_length / max_length;
        if (weighted) w *= reference_weights[k];
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

// what one cue is worth at one offset. the reference is sorted and does not overlap itself so a binary search finds the few spans that can touch it
static double span_score(const std::pair<int,int>& span, const std::vector<std::pair<int,int>>& reference_spans, const std::vector<float>& reference_weights, int x) {
    bool weighted = reference_weights.size() == reference_spans.size();
    int from = span.first + x;
    int to = span.second + x;

    int lo = (int)(std::lower_bound(reference_spans.begin(), reference_spans.end(), std::make_pair(from, from),
        [](const std::pair<int,int>& a, const std::pair<int,int>& b) { return a.second < b.second; }) - reference_spans.begin());

    double total = 0;
    for (int k = lo; k < (int)reference_spans.size() && reference_spans[k].first < to; k++) {
        int overlap = std::min(reference_spans[k].second, to) - std::max(reference_spans[k].first, from);
        if (overlap <= 0) continue;

        int a = reference_spans[k].second - reference_spans[k].first;
        int b = to - from;
        int shortest = std::min(a, b);
        int longest = std::max(a, b);
        if (shortest <= 0) continue;

        double w = (double)shortest / longest;
        if (weighted) w *= reference_weights[k];
        total += w * overlap / shortest;
    }
    return total;
}

// the dp puts a boundary anywhere inside a stretch with no dialogue, because every position there scores the same. slide it to where it actually pays,
// and when that is still a tie put it in the biggest gap between two cues a film gets recut at a scene change, not in the middle of a conversation
static void settle_boundaries(std::vector<int>& offsets, const std::vector<std::pair<int,int>>& read_srt, const std::vector<std::pair<int,int>>& reference_spans, const std::vector<float>& reference_weights) {
    const int REACH = 60;
    int n = (int)offsets.size();

    for (int i = 1; i < n; i++) {
        if (offsets[i] == offsets[i - 1]) continue;

        int before = offsets[i - 1];
        int after = offsets[i];
        int from = std::max(1, i - REACH);
        int to = std::min(n - 1, i + REACH);

        // walking the boundary right moves one cue from the after side to the before side, so the running total only changes by that one cue
        double running = 0;
        for (int k = from; k < i; k++) running += span_score(read_srt[k], reference_spans, reference_weights, before);
        for (int k = i; k <= to; k++) running += span_score(read_srt[k], reference_spans, reference_weights, after);

        double best = running;
        int at = i;
        int widest = read_srt[i].first - read_srt[i - 1].second;

        double walk = running;
        for (int j = i + 1; j <= to; j++) {
            walk += span_score(read_srt[j - 1], reference_spans, reference_weights, before)
                  - span_score(read_srt[j - 1], reference_spans, reference_weights, after);
            int gap = read_srt[j].first - read_srt[j - 1].second;
            if (walk > best + 1e-9 || (walk > best - 1e-9 && gap > widest)) {
                best = std::max(best, walk);
                widest = gap;
                at = j;
            }
        }

        walk = running;
        for (int j = i - 1; j >= from; j--) {
            walk += span_score(read_srt[j], reference_spans, reference_weights, after)
                  - span_score(read_srt[j], reference_spans, reference_weights, before);
            int gap = read_srt[j].first - read_srt[j - 1].second;
            if (walk > best + 1e-9 || (walk > best - 1e-9 && gap > widest)) {
                best = std::max(best, walk);
                widest = gap;
                at = j;
            }
        }

        for (int k = std::min(at, i); k < std::max(at, i); k++)
            offsets[k] = (k < at) ? before : after;
        i = std::max(at, i);
    }
}

std::vector<int> split_alignment(const std::vector<std::pair<int,int>>& read_srt, const std::vector<std::pair<int,int>>& reference_spans, const std::vector<float>& reference_weights, float p, int base_offset, int window_ms, int step_ms) {

    int lo = base_offset - window_ms;
    int hi = base_offset + window_ms;

    std::vector<double> t_prev = score_curve(read_srt[0], reference_spans, reference_weights, lo, hi, step_ms);
    std::vector<std::vector<int>> all_to;

    for (int n = 1; n < (int)read_srt.size(); n++) {
        if (n % 64 == 0) progress("Looking for cuts", n, (int)read_srt.size());
        std::vector<double> scores = score_curve(read_srt[n], reference_spans, reference_weights, lo, hi, step_ms);
        std::vector<double> t_new(scores.size(), 0);
        std::vector<int> to(scores.size(), 0);


        int gap = (read_srt[n].first - read_srt[n-1].second) / step_ms;
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

            if (allowed < 0) {
                t_new[sigma] = -std::numeric_limits<double>::infinity();
                to[sigma] = 0;
            } else if (gap >= 0 && t_prev[sigma] >= s_max - p) {
                //Staying where the previous cue sits is free, moving somewhere else costs the split penalty
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

    std::vector<int> offsets(read_srt.size());
    offsets[read_srt.size() - 1] = lo + sigma_best * step_ms;

    for (int n = all_to.size() - 1; n >= 0; n--) {
        sigma_best = all_to[n][sigma_best];
        offsets[n] = lo + sigma_best * step_ms;
    }

    int from = 0;
    while (from < (int)offsets.size()) {
        int to = from;
        while (to + 1 < (int)offsets.size() && offsets[to + 1] == offsets[from]) to++;

        std::vector<std::pair<int, int>> part;
        for (int k = from; k <= to; k++)
            part.push_back({read_srt[k].first + offsets[from], read_srt[k].second + offsets[from]});

        Lock got = best_offset(part, reference_spans, reference_weights);
        if (std::abs(got.offset) <= 2 * step_ms)
            for (int k = from; k <= to; k++) offsets[k] += got.offset;

        from = to + 1;
    }

    settle_boundaries(offsets, read_srt, reference_spans, reference_weights);
    progress_done();
    return offsets;
}

