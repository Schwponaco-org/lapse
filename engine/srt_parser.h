#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

std::pair<std::vector<std::pair<int,int>>, std::vector<int>> read_srt(const char* filename);
std::vector<int> activity(std::vector<std::pair<int, int>> spans);
void write_srt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s);