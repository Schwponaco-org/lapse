#include "correlate.h"

// slope og intercept for y = slope*x + intercept
std::pair<double, double> linear_regression(std::vector<double> x, std::vector<double> y, std::vector<double> w) {
    double sw = 0, swx = 0 , swy = 0, swxx = 0, swxy = 0;
    for (int i = 0; i < x.size(); i++) {
        sw   += w[i];
        swx  += w[i] * x[i];
        swy  += w[i] * y[i];
        swxx += w[i] * x[i] * x[i];
        swxy += w[i] * x[i] * y[i];
    }
    double slope = (sw*swxy - swx*swy) / (sw*swxx - swx*swx);
    double intercept = (swy - slope*swx) / sw;
    return {slope, intercept};
}

std::pair<double, double> fft_crosscorrelate(std::vector<int> activity_profile, std::vector<int> srt_profile) {
    int padded = 262144;
    int chunk_size = 90000;
    int half = padded / 2 + 1;

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

        // Fill activity buffer then full the rest with zeros
        for (int i = 0; i < padded; ++i)
            activity_buf[i] = (i < chunk_size) ? activity_profile[chunks + i] : 0.0;

        // Fill srt buffer same window then zeros
        for (int i = 0; i < padded; ++i)
            srt_buf[i] = (i < chunk_size && chunks + i < (int)srt_profile.size()) ? srt_profile[chunks + i] : 0.0;

  
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

        // Find peak and second best peak
        double best_val = -std::numeric_limits<double>::infinity();
        double second_val = -std::numeric_limits<double>::infinity();
        int best_idx = 0;

        for (int i = 0; i < padded; ++i) {
            if (corr_out[i] > best_val) {
                best_val = corr_out[i];
                best_idx = i;
            } else if (corr_out[i] > second_val) {
                second_val = corr_out[i];
            }
        }


        //Convert index to offset in ms
        //Positive offsets index 0 to chunk_size-1
        //Negative offsets index padded-chunk_size to padded-1
        double offset_ms;

        if (best_idx < padded / 2)
            offset_ms = best_idx * 10.0;
        else
            offset_ms = (best_idx - padded) * 10.0;


        double sharpness = best_val / second_val;

        std::cout << "t_" << chunk_number << " offset: " << offset_ms << "ms\n";
        std::cout << "Sharpness_" << chunk_number << ": " << sharpness << '\n';

        t_vals.push_back((chunk_number - 0.5) * 900);
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