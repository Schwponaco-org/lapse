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

// Sliding the cues past the speech one pair at a time is O(cues * spans) and on
// a long film that is forty million events to sort. The same answer falls out
// of one cross correlation, and once it is an fft we can afford to do it on two
// channels at once: where the talking is, and where it starts.
//
// The second thing this fixes is worse than the speed. A plain overlap score
// asks "how many of my cues landed on speech", which is highest wherever the
// film talks the most, whether or not anything lines up. Films with a quiet
// half hour and a busy one - Godfather, Saving Private Ryan, Tree of Life -
// grow a hill in the score that has nothing to do with sync and the real spike
// sits on the side of it. Taking a half minute running mean out of both signals
// first leaves only the part that is actually about lining up.

#include "align.h"
#include "correlate.h"
#include "log.h"

#include <fftw3.h>
#include <algorithm>
#include <cmath>
#include <random>

static const int G = 40;    // one bin, in ms
static const int HP_MS = 30000; // running mean taken out of the block channel
static const int HP_ON_MS = 8000;
static const double ON_W = 1.15;
static const double ONSET_SM = 120;
static const double TAU = 450;

static int nfft = 0;
static int reach = 0;
static int ref_bins = 0;
static std::vector<std::pair<int,int>> ref;
static std::vector<float> ref_w;
static std::vector<int> starts;

static double *in_buf = nullptr, *out_buf = nullptr;
static fftw_complex *rb = nullptr, *ro = nullptr, *sb = nullptr, *so = nullptr, *mix = nullptr;
static fftw_plan fwd = nullptr, back = nullptr;
static double rb_norm = 1, ro_norm = 1;
static std::vector<double> last_curve;
static double last_mid = 0, last_spread = 1;

static int pow2(int n) {
    int p = 256;
    while (p < n) p <<= 1;
    return p;
}

static void smooth(std::vector<double>& v, int n, double sigma_ms) {
    int r = (int)(3 * sigma_ms / G);
    if (r < 1) return;
    double s = sigma_ms / G;
    std::vector<double> k(2 * r + 1);
    double sum = 0;
    for (int j = -r; j <= r; j++) {
        k[j + r] = std::exp(-0.5 * j * j / (s * s));
        sum += k[j + r];
    }
    for (double& x : k) x /= sum;

    std::vector<double> o(n, 0.0);
    for (int i = 0; i < n; i++) {
        if (v[i] == 0.0) continue;
        int a = std::max(0, i - r), b = std::min(n - 1, i + r);
        for (int j = a; j <= b; j++) o[j] += v[i] * k[j - i + r];
    }
    for (int i = 0; i < n; i++) v[i] = o[i];
}

static void highpass(std::vector<double>& v, int n, int w) {
    if (n < 4) return;
    std::vector<double> pre(n + 1, 0.0);
    for (int i = 0; i < n; i++) pre[i + 1] = pre[i] + v[i];
    for (int i = 0; i < n; i++) {
        int a = i - w / 2, b = i + w / 2;
        if (a < 0) a = 0;
        if (b > n) b = n;
        v[i] -= (pre[b] - pre[a]) / (b - a);
    }
}

static double energy(const std::vector<double>& v, int n) {
    double e = 0;
    for (int i = 0; i < n; i++) e += v[i] * v[i];
    return std::sqrt(e) + 1e-12;
}

static void run_fwd(std::vector<double>& v, fftw_complex* to) {
    for (int i = 0; i < nfft; i++) in_buf[i] = v[i];
    fftw_execute_dft_r2c(fwd, in_buf, to);
}

bool align_ready() { return nfft > 0; }
int align_reach() { return reach; }

void align_drop() {
    if (fwd) fftw_destroy_plan(fwd);
    if (back) fftw_destroy_plan(back);
    for (fftw_complex* c : {rb, ro, sb, so, mix}) if (c) fftw_free(c);
    if (in_buf) fftw_free(in_buf);
    if (out_buf) fftw_free(out_buf);
    fwd = back = nullptr;
    rb = ro = sb = so = mix = nullptr;
    in_buf = out_buf = nullptr;
    nfft = 0;
}

void align_setup(const std::vector<std::pair<int,int>>& spans, const std::vector<float>& weights) {
    align_drop();
    if (spans.size() < 4) return;

    ref = spans;
    ref_w = weights;
    starts.clear();
    for (auto& s : ref) starts.push_back(s.first);

    ref_bins = ref.back().second / G + 2;
    reach = MAX_OFFSET_MS;
    nfft = pow2(ref_bins + ref_bins / 4 + reach / G + 64);

    int half = nfft / 2 + 1;
    in_buf = (double*)fftw_malloc(sizeof(double) * nfft);
    out_buf = (double*)fftw_malloc(sizeof(double) * nfft);
    rb = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * half);
    ro = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * half);
    sb = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * half);
    so = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * half);
    mix = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * half);

    fwd = fftw_plan_dft_r2c_1d(nfft, in_buf, rb, FFTW_ESTIMATE);
    back = fftw_plan_dft_c2r_1d(nfft, mix, out_buf, FFTW_ESTIMATE);

    std::vector<double> b(nfft, 0.0), o(nfft, 0.0);
    bool weighted = ref_w.size() == ref.size();

    for (size_t k = 0; k < ref.size(); k++) {
        double h = weighted ? ref_w[k] : 1.0;
        int a = ref[k].first / G, e = ref[k].second / G;
        if (a < 0) a = 0;
        if (e >= ref_bins) e = ref_bins - 1;
        for (int i = a; i <= e; i++) b[i] = h;
        if (a < ref_bins) o[a] += h;
    }

    smooth(o, ref_bins, ONSET_SM);
    highpass(b, ref_bins, HP_MS / G);
    highpass(o, ref_bins, HP_ON_MS / G);

    rb_norm = energy(b, ref_bins);
    ro_norm = energy(o, ref_bins);

    run_fwd(b, rb);
    run_fwd(o, ro);
}


static int build(const std::vector<std::pair<int,int>>& cues, std::vector<double>& b, std::vector<double>& o) {
    int len = 0;
    for (auto& c : cues) len = std::max(len, c.second / G + 2);
    if (len > nfft - reach / G - 8) len = nfft - reach / G - 8;
    if (len < 4) return 0;

    b.assign(nfft, 0.0);
    o.assign(nfft, 0.0);

    for (auto& c : cues) {
        int a = c.first / G, e = c.second / G;
        if (a < 0) a = 0;
        if (a >= len) continue;
        if (e >= len) e = len - 1;
        if (e < a) e = a;
        double h = 1.0 / (e - a + 1);
        for (int i = a; i <= e; i++) b[i] += h;
        o[a] += 1.0;
    }

    smooth(o, len, ONSET_SM);
    highpass(b, len, HP_MS / G);
    highpass(o, len, HP_ON_MS / G);
    return len;
}

static void peaks_from(const std::vector<double>& curve, int lag, int want, std::vector<Hit>& out) {
    if (curve.empty()) return;

    std::vector<double> noise;
    for (size_t i = 0; i < curve.size(); i += 3) noise.push_back(curve[i]);
    std::sort(noise.begin(), noise.end());
    double mid = noise[noise.size() / 2];

    std::vector<double> away;
    for (double v : noise) away.push_back(std::abs(v - mid));
    std::sort(away.begin(), away.end());
    double spread = 1.4826 * away[away.size() / 2];
    if (spread < 1e-12) spread = 1e-12;

    last_curve = curve;
    last_mid = mid;
    last_spread = spread;

    std::vector<char> taken(curve.size(), 0);
    int guard = 2500 / G;
    std::vector<std::pair<int,double>> tops;

    for (int n = 0; n < want + 1; n++) {
        int at = -1;
        double best = -1e300;
        for (int i = 0; i < (int)curve.size(); i++) {
            if (taken[i]) continue;
            if (curve[i] > best) { best = curve[i]; at = i; }
        }
        if (at < 0) break;
        tops.push_back({at, best});
        for (int i = std::max(0, at - guard); i < std::min((int)curve.size(), at + guard); i++) taken[i] = 1;
    }
    if (tops.empty()) return;

    double top = tops[0].second - mid;
    for (int n = 0; n < (int)tops.size() && n < want; n++) {
        Hit h;
        h.offset = (tops[n].first - lag) * G;
        h.z = (tops[n].second - mid) / spread;
        h.runner = (top > 0 && n + 1 < (int)tops.size()) ? (tops[n + 1].second - mid) / top : 0.0;
        if (h.runner < 0) h.runner = 0;
        out.push_back(h);
    }
}


std::vector<Hit> align_peaks(const std::vector<std::pair<int,int>>& cues, int want) {
    std::vector<Hit> found;
    if (!nfft || cues.empty()) return found;

    std::vector<double> b, o;
    int len = build(cues, b, o);
    if (!len) return found;

    double bn = energy(b, len), on = energy(o, len);
    run_fwd(b, sb);
    run_fwd(o, so);

    int half = nfft / 2 + 1;
    double wb = 1.0 / (rb_norm * bn);
    double wo = ON_W / (ro_norm * on);

    int lag = reach / G;
    static const double blurs[4] = {0, 550, 450, 250};
    static const int only[4] = {2, 2, 1, 0};

    for (int pass = 0; pass < 4; pass++) {
        double blur = blurs[pass] / G;
        for (int k = 0; k < half; k++) {
            double br = rb[k][0] * sb[k][0] + rb[k][1] * sb[k][1];
            double bi = rb[k][1] * sb[k][0] - rb[k][0] * sb[k][1];
            double orr = ro[k][0] * so[k][0] + ro[k][1] * so[k][1];
            double oi = ro[k][1] * so[k][0] - ro[k][0] * so[k][1];
            double re = 0, im = 0;
            if (only[pass] != 1) { re += br * wb; im += bi * wb; }
            if (only[pass] != 0) { re += orr * wo; im += oi * wo; }

            double g = 1.0;
            if (blur > 0) {
                double f = (double)k / nfft;
                g = std::exp(-2.0 * M_PI * M_PI * blur * blur * f * f);
            }
            mix[k][0] = re * g;
            mix[k][1] = im * g;
        }
        fftw_execute_dft_c2r(back, mix, out_buf);

        std::vector<double> curve(2 * lag + 1);
        for (int m = -lag; m <= lag; m++) {
            int at = (m >= 0) ? m : m + nfft;
            curve[m + lag] = out_buf[at] / nfft;
        }
        peaks_from(curve, lag, want, found);
    }

    std::sort(found.begin(), found.end(), [](const Hit& a, const Hit& c) { return a.z > c.z; });

    std::vector<Hit> tidy;
    for (auto& h : found) {
        bool near = false;
        for (auto& k : tidy) if (std::abs(k.offset - h.offset) < 2000) near = true;
        if (!near) tidy.push_back(h);
        if ((int)tidy.size() >= want * 4) break;
    }
    return tidy;
}

static double one_cue(const std::pair<int,int>& c, int x) {
    bool weighted = ref_w.size() == ref.size();
    int from = c.first + x, to = c.second + x;
    if (to <= from) return 0;

    int lo = (int)(std::lower_bound(ref.begin(), ref.end(), std::make_pair(from, from),
        [](const std::pair<int,int>& a, const std::pair<int,int>& b) { return a.second < b.second; }) - ref.begin());

    double total = 0;
    for (int k = lo; k < (int)ref.size() && ref[k].first < to; k++) {
        int over = std::min(ref[k].second, to) - std::max(ref[k].first, from);
        if (over <= 0) continue;
        int a = ref[k].second - ref[k].first, b = to - from;
        int shortest = std::min(a, b), longest = std::max(a, b);
        if (shortest <= 0) continue;
        double w = (double)shortest / longest;
        if (weighted) w *= ref_w[k];
        total += w * over / shortest;
    }
    return total;
}

double align_score(const std::vector<std::pair<int,int>>& cues, int off) {
    double t = 0;
    for (auto& c : cues) t += one_cue(c, off);
    return t;
}

static double nearest_start(int t) {
    if (starts.empty()) return 1e9;
    size_t i = std::lower_bound(starts.begin(), starts.end(), t) - starts.begin();
    double best = 1e9;
    if (i < starts.size()) best = std::abs(starts[i] - t);
    if (i > 0) best = std::min(best, (double)std::abs(starts[i - 1] - t));
    return best;
}


static double bunching(const std::vector<std::pair<int,int>>& cues, int off) {
    double t = 0;
    for (auto& c : cues) {
        double d = nearest_start(c.first + off);
        t += std::exp(-0.5 * d * d / (TAU * TAU));
    }
    return t / cues.size();
}

int align_refine(const std::vector<std::pair<int,int>>& cues, int off, int reach_ms) {
    if (cues.empty()) return off;

    int best = off;
    double top = -1e300;
    for (int d = -reach_ms; d <= reach_ms; d += 5) {
        double s = align_score(cues, off + d);
        if (s > top) { top = s; best = off + d; }
    }
    int coarse = best;
    for (int d = -6; d <= 6; d++) {
        double s = align_score(cues, coarse + d);
        if (s > top) { top = s; best = coarse + d; }
    }
    return best;
}

static void slice_baseline(const std::vector<std::pair<int,int>>& cues);

static double sigmas(double got, double mean, double sd) {
    if (sd < 1e-9) sd = 1e-9;
    double z = (got - mean) / sd;
    return z < 0 ? 0 : z;
}

static const void* base_key = nullptr;
static size_t base_len = 0;
static int base_edge = 0;
static double bunch_mean = 0, bunch_sd = 1, over_mean = 0, over_sd = 1;

static void baseline(const std::vector<std::pair<int,int>>& cues) {
    if (base_key == cues.data() && base_len == cues.size() && base_edge == cues.back().second) return;
    base_key = cues.data();
    base_len = cues.size();
    base_edge = cues.back().second;

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> pick(-900000, 900000);
    std::vector<double> bunch, over;
    for (int i = 0; i < 96; i++) {
        int x = pick(rng);
        bunch.push_back(bunching(cues, x));
        over.push_back(align_score(cues, x) / cues.size());
    }

    auto stats = [](const std::vector<double>& v, double& m, double& sd) {
        m = 0;
        for (double q : v) m += q;
        m /= v.size();
        double var = 0;
        for (double q : v) var += (q - m) * (q - m);
        sd = std::sqrt(var / v.size());
    };
    stats(bunch, bunch_mean, bunch_sd);
    stats(over, over_mean, over_sd);
    slice_baseline(cues);
}

static const int SLICES = 8;
static std::vector<double> sl_bm, sl_bsd, sl_om, sl_osd;

static void slice_baseline(const std::vector<std::pair<int,int>>& cues) {
    sl_bm.assign(SLICES, 0); sl_bsd.assign(SLICES, 1);
    sl_om.assign(SLICES, 0); sl_osd.assign(SLICES, 1);

    int per = (int)cues.size() / SLICES;
    if (per < 8) { sl_bm.clear(); return; }

    std::mt19937 rng(777);
    std::uniform_int_distribution<int> pick(-900000, 900000);
    std::vector<int> draws;
    for (int i = 0; i < 48; i++) draws.push_back(pick(rng));

    for (int c = 0; c < SLICES; c++) {
        std::vector<std::pair<int,int>> part(cues.begin() + c * per,
            (c == SLICES - 1) ? cues.end() : cues.begin() + (c + 1) * per);
        std::vector<double> b, o;
        for (int x : draws) {
            b.push_back(bunching(part, x));
            o.push_back(align_score(part, x) / part.size());
        }
        auto stats = [](const std::vector<double>& v, double& m, double& sd) {
            m = 0;
            for (double q : v) m += q;
            m /= v.size();
            double var = 0;
            for (double q : v) var += (q - m) * (q - m);
            sd = std::sqrt(var / v.size());
            if (sd < 1e-9) sd = 1e-9;
        };
        stats(b, sl_bm[c], sl_bsd[c]);
        stats(o, sl_om[c], sl_osd[c]);
    }
}

static double slice_support(const std::vector<std::pair<int,int>>& cues, int off) {
    if (sl_bm.empty()) return 0;
    int per = (int)cues.size() / SLICES;
    double total = 0;

    for (int c = 0; c < SLICES; c++) {
        std::vector<std::pair<int,int>> part(cues.begin() + c * per,
            (c == SLICES - 1) ? cues.end() : cues.begin() + (c + 1) * per);
        double zb = (bunching(part, off) - sl_bm[c]) / sl_bsd[c];
        double zo = (align_score(part, off) / part.size() - sl_om[c]) / sl_osd[c];
        double z = (zb + zo) * 0.5;
        if (z > 3.0) z = 3.0;
        if (z < -1.0) z = -1.0;
        total += z;
    }
    return total / SLICES;
}

double onset_z(const std::vector<std::pair<int,int>>& cues, int off, double* rate) {
    if (cues.size() < 8 || ref.size() < 8) return 0;
    baseline(cues);

    double got_bunch = bunching(cues, off);
    double got_over = align_score(cues, off) / cues.size();
    if (rate) *rate = got_bunch;

    double solo = (sigmas(got_bunch, bunch_mean, bunch_sd) + sigmas(got_over, over_mean, over_sd)) * 0.6;
    return solo + 1.6 * slice_support(cues, off);
}
