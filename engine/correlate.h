#pragma once
#include <vector>
#include <iostream>
#include <limits>
#include <tuple>
#include <utility>
#include <fftw3.h>

std::pair<double, double> linear_regression(std::vector<double> x, std::vector<double> y, std::vector<double> w);
std::pair<double, double> fft_crosscorrelate(std::vector<int> activity_profile, std::vector<int> srt_profile);

