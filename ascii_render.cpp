#include "ascii_render.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <vector>
#include <opencv2/opencv.hpp>
#include <immintrin.h>



static inline void compute_gray_line_scalar(
    const unsigned char* b, const unsigned char* g, const unsigned char* r,
    unsigned char* gray, int width)
{
    for (int x = 0; x < width; ++x) {
        unsigned int rr = r[x];
        unsigned int gg = g[x];
        unsigned int bb = b[x];
        unsigned int y  = rr * 77 + gg * 150 + bb * 29;
        gray[x] = static_cast<unsigned char>(y >> 8);
    }
}

static inline void compute_gray_line_avx2(
    const unsigned char* b, const unsigned char* g, const unsigned char* r,
    unsigned char* gray, int width)
{
#if defined(__AVX2__)
    const __m256i cR = _mm256_set1_epi16(77);
    const __m256i cG = _mm256_set1_epi16(150);
    const __m256i cB = _mm256_set1_epi16(29);

    int x = 0;
    for (; x + 32 <= width; x += 32) {
        __m128i b_lo8 = _mm_loadu_si128((const __m128i*)(b + x));
        __m128i b_hi8 = _mm_loadu_si128((const __m128i*)(b + x + 16));
        __m128i g_lo8 = _mm_loadu_si128((const __m128i*)(g + x));
        __m128i g_hi8 = _mm_loadu_si128((const __m128i*)(g + x + 16));
        __m128i r_lo8 = _mm_loadu_si128((const __m128i*)(r + x));
        __m128i r_hi8 = _mm_loadu_si128((const __m128i*)(r + x + 16));

        __m256i b_lo16 = _mm256_cvtepu8_epi16(b_lo8);
        __m256i b_hi16 = _mm256_cvtepu8_epi16(b_hi8);
        __m256i g_lo16 = _mm256_cvtepu8_epi16(g_lo8);
        __m256i g_hi16 = _mm256_cvtepu8_epi16(g_hi8);
        __m256i r_lo16 = _mm256_cvtepu8_epi16(r_lo8);
        __m256i r_hi16 = _mm256_cvtepu8_epi16(r_hi8);

        __m256i y_lo = _mm256_add_epi16(_mm256_add_epi16(_mm256_mullo_epi16(r_lo16, cR),
                                                         _mm256_mullo_epi16(g_lo16, cG)),
                                                         _mm256_mullo_epi16(b_lo16, cB));
        __m256i y_hi = _mm256_add_epi16(_mm256_add_epi16(_mm256_mullo_epi16(r_hi16, cR),
                                                         _mm256_mullo_epi16(g_hi16, cG)),
                                                         _mm256_mullo_epi16(b_hi16, cB));

        __m256i y_lo_shift = _mm256_srli_epi16(y_lo, 8);
        __m256i y_hi_shift = _mm256_srli_epi16(y_hi, 8);
        __m256i y_u8 = _mm256_packus_epi16(y_lo_shift, y_hi_shift);

        _mm256_storeu_si256((__m256i*)(gray + x), y_u8);
    }
    if (x < width) {
        compute_gray_line_scalar(b + x, g + x, r + x, gray + x, width - x);
    }
#else
    compute_gray_line_scalar(b, g, r, gray, width);
#endif
}

namespace ascii_render {

    static std::vector<std::string> prev_lines;

    size_t render_frame(const std::string& frame) {
        std::vector<std::string> lines;
        size_t pos = 0, prev = 0;
        while ((pos = frame.find('\n', prev)) != std::string::npos) {
            lines.push_back(frame.substr(prev, pos - prev));
            prev = pos + 1;
        }
        if (prev < frame.size()) {
            lines.push_back(frame.substr(prev));
        }

        std::cout << "\x1b[H";

        size_t updated_lines = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i >= prev_lines.size() || lines[i] != prev_lines[i]) {
                std::cout << "\x1b[" << (i + 1) << ";1H" << lines[i] << "\x1b[0m";
                ++updated_lines;
            }
        }
        std::cout.flush();
        prev_lines = lines;
        return updated_lines;
    }

    std::string frame_to_ascii_mono(
        const cv::Mat& frame,
        bool is_paused,
        double progress,
        double current_time,
        double total_time,
        int volume
    ) {
        if (frame.channels() != 3 && frame.channels() != 4) {
            throw std::runtime_error("Expected 3- or 4-channel BGR(A) image");
        }

        int height = frame.rows;
        int width = frame.cols;

        cv::Mat gray;
        if (frame.channels() == 3)
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        else 
            cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);

        std::ostringstream oss;

        auto gray_to_ascii = [](uchar value) -> char {
            static const char* chars = " `.,:;_-~\"><|!/)(^?}{][*=clsji+o2r7fC1xekJutFyVnLzS53TmG4PhaqEwYvZ96bdpg0OUAXNDQRKHM8B&%$W#@";
            size_t len = strlen(chars);
            int index = static_cast<int>((value * (len - 1)) / 255);
            return chars[index];
        };

        for (int y = 0; y < gray.rows; ++y) {
            for (int x = 0; x < gray.cols; ++x) {
                uchar pixel = gray.at<uchar>(y, x);
                oss << gray_to_ascii(pixel);
            }
            oss << "\n";
        }

        auto format_time = [](double seconds) -> std::string {
            int total_sec = static_cast<int>(seconds);
            int m = total_sec / 60;
            int s = total_sec % 60;
            char buffer[6];
            snprintf(buffer, sizeof(buffer), "%02d:%02d", m, s);
            return std::string(buffer);
        };

        std::string current_time_str = format_time(current_time);
        std::string total_time_str = format_time(total_time);
        std::string time_str = " " + current_time_str + " / " + total_time_str;

        std::string status = is_paused ? "||" : "|>";

        int bar_width = width;
        if (bar_width < 10) bar_width = 10;

        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;

        int filled_length = static_cast<int>(progress * bar_width);
        std::string progress_bar(filled_length, '#');
        progress_bar += std::string(bar_width - filled_length, '-');

        oss << progress_bar << "\n";

        std::string volume_str = "Vol: " + std::to_string(volume) + "% ";
        int total_len = width;
        int time_len = (int)time_str.length();
        int status_len = 2;
        int volume_len = (int)volume_str.length();
        int status_start_pos = total_len / 2 - status_len / 2;

        std::string line(total_len, ' ');
        for (int i = 0; i < time_len && i < total_len; ++i) {
            line[i] = time_str[i];
        }
        for (int i = 0; i < volume_len && (total_len - volume_len + i) < total_len; ++i) {
            line[total_len - volume_len + i] = volume_str[i];
        }
        if (status_start_pos >= 0 && status_start_pos + status_len <= total_len)
            line.replace(status_start_pos, status_len, status);
        oss << line << "\n";

        return oss.str();
    }

    std::string frame_to_ascii_color(
        const cv::Mat& frame,
        bool is_paused,
        double progress,
        double current_time,
        double total_time,
        int volume,
        int num_threads
    ) {
        static const char* chars = " `.,:;_-~\"><|!/)(^?}{][*=clsji+o2r7fC1xekJutFyVnLzS53TmG4PhaqEwYvZ96bdpg0OUAXNDQRKHM8B&%$W#@";
        const size_t chars_len = strlen(chars);

        const int width  = frame.cols;
        const int height = frame.rows;

        CV_Assert(frame.type() == CV_8UC3);
        CV_Assert(frame.isContinuous());

        if (num_threads < 1) num_threads = 1;
        if (num_threads > height) num_threads = height;

        std::vector<cv::Mat> ch(3);
        cv::split(frame, ch);

        std::vector<std::string> thread_buffers(num_threads);
        std::vector<std::thread> workers;
        workers.reserve(num_threads);

        auto worker = [&](int start_y, int end_y, std::string& out) {
            const unsigned char* B = ch[0].ptr<unsigned char>(0);
            const unsigned char* G = ch[1].ptr<unsigned char>(0);
            const unsigned char* R = ch[2].ptr<unsigned char>(0);

            const int stride = width;

            out.reserve((end_y - start_y) * width * 28);

            std::vector<unsigned char> gray_line(width);

            for (int y = start_y; y < end_y; ++y) {
                const unsigned char* b_row = B + y * stride;
                const unsigned char* g_row = G + y * stride;
                const unsigned char* r_row = R + y * stride;

                compute_gray_line_avx2(b_row, g_row, r_row, gray_line.data(), width);

                int last_r = -1, last_g = -1, last_b = -1;

                for (int x = 0; x < width; ++x) {
                    unsigned char r = r_row[x];
                    unsigned char g = g_row[x];
                    unsigned char b = b_row[x];

                    unsigned char gray = gray_line[x];
                    char ch = chars[(gray * (chars_len - 1)) / 255];

                    if (r != last_r || g != last_g || b != last_b) {
                        char esc[32];
                        int esc_len = snprintf(esc, sizeof(esc), "\x1b[38;2;%u;%u;%um", (unsigned)r, (unsigned)g, (unsigned)b);
                        out.append(esc, esc_len);
                        last_r = r; last_g = g; last_b = b;
                    }
                    out.push_back(ch);
                }
                out.append("\x1b[0m\n", 5);
            }
        };

        int base = height / num_threads;
        int rem  = height % num_threads;
        int y0 = 0;

        for (int t = 0; t < num_threads; ++t) {
            int start_y = y0;
            int end_y   = start_y + base + (t < rem ? 1 : 0);
            y0 = end_y;
            workers.emplace_back(worker, start_y, end_y, std::ref(thread_buffers[t]));
        }
        for (auto& th : workers) th.join();

        size_t total = 0;
        for (auto& s : thread_buffers) total += s.size();

        std::string buffer;
        buffer.reserve(total + width + 128);
        for (auto& s : thread_buffers) buffer.append(s);

        int bar_width = std::max(10, width);
        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;
        int filled = static_cast<int>(progress * bar_width);

        buffer.append(std::string(filled, '#'));
        buffer.append(std::string(bar_width - filled, '-'));
        buffer.push_back('\n');

        auto format_time = [](double seconds) -> std::string {
            int total_sec = (int)seconds;
            int m = total_sec / 60;
            int s = total_sec % 60;
            char buf[6];
            snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
            return std::string(buf);
        };

        std::string time_str   = " " + format_time(current_time) + " / " + format_time(total_time);
        std::string volume_str = "Vol: " + std::to_string(volume) + "% ";
        std::string status     = is_paused ? "||" : "|>";

        std::string line(width, ' ');
        std::copy(time_str.begin(), time_str.end(), line.begin());
        
        std::copy(volume_str.begin(), volume_str.end(), line.end() - volume_str.size());

        int status_pos = width / 2 - 1;
        if (status_pos >= 0 && status_pos + (int)status.size() <= width)
            std::copy(status.begin(), status.end(), line.begin() + status_pos);

        buffer.append(line);
        buffer.push_back('\n');

        return buffer;
    }
}

