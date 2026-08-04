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

#pragma once
#include <vector>
#include <iostream>
#include <limits>
#include <tuple>
#include <utility>
#include <fftw3.h>
#include <algorithm>

std::pair<double, double> linear_regression(std::vector<double> x, std::vector<double> y, std::vector<double> w);
std::pair<double, double> fft_crosscorrelate(std::vector<int> activity_profile, std::vector<int> srt_profile);
int score_calculator(std::vector<std::pair<int, int>> read_srt, std::vector<std::pair<int, int>> reference_spans, int x);
int best_offset(std::vector<std::pair<int, int>> read_srt, std::vector<std::pair<int, int>> reference_spans);
std::vector<float> score_curve(std::vector<std::pair<int,int>> span, std::vector<std::pair<int,int>> reference_spans);
std::vector<int> split_alignment(std::vector<std::pair<int,int>> read_srt, std::vector<std::pair<int,int>> reference_spans, float p);

