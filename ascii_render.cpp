#include "ascii_render.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <vector>
#include <opencv2/opencv.hpp>

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

        const int width = frame.cols;
        const int height = frame.rows;

        if (num_threads < 1) num_threads = 1;
        if (num_threads > height) num_threads = height;

        std::vector<std::string> thread_buffers(num_threads);
        std::vector<std::thread> workers;
        workers.reserve(num_threads);

        auto worker = [&](int start_y, int end_y, std::string& local_buf) {
            local_buf.reserve((end_y - start_y) * width * 30);
            for (int y = start_y; y < end_y; ++y) {
                const cv::Vec3b* row_ptr = frame.ptr<cv::Vec3b>(y);
                int last_r = -1, last_g = -1, last_b = -1;

                for (int x = 0; x < width; ++x) {
                    uchar b = row_ptr[x][0];
                    uchar g = row_ptr[x][1];
                    uchar r = row_ptr[x][2];

                    uchar gray = static_cast<uchar>(r * 0.299 + g * 0.587 + b * 0.114);
                    char ch = chars[(gray * (chars_len - 1)) / 255];

                    if (r != last_r || g != last_g || b != last_b) {
                        char esc[32];
                        int esc_len = snprintf(esc, sizeof(esc), "\x1b[38;2;%d;%d;%dm", r, g, b);
                        local_buf.append(esc, esc_len);
                        last_r = r; last_g = g; last_b = b;
                    }
                    local_buf.push_back(ch);
                }
                local_buf.append("\x1b[0m\n", 5);
            }
        };

        // Запуск потоков
        int block_size = height / num_threads;
        int remainder = height % num_threads;
        int current_y = 0;

        for (int t = 0; t < num_threads; ++t) {
            int start_y = current_y;
            int end_y = start_y + block_size + (t < remainder ? 1 : 0);
            current_y = end_y;
            workers.emplace_back(worker, start_y, end_y, std::ref(thread_buffers[t]));
        }

        for (auto& th : workers) th.join();

        // Склеиваем всё в один буфер
        std::string buffer;
        size_t total_reserve = 0;
        for (auto& part : thread_buffers) total_reserve += part.size();
        buffer.reserve(total_reserve + 500);

        for (auto& part : thread_buffers) buffer.append(part);

        // Прогресс-бар
        int bar_width = std::max(10, width);
        progress = std::clamp(progress, 0.0, 1.0);
        int filled_length = static_cast<int>(progress * bar_width);

        buffer.append(std::string(filled_length, '#'));
        buffer.append(std::string(bar_width - filled_length, '-'));
        buffer.push_back('\n');

        // Время и статус
        auto format_time = [](double seconds) -> std::string {
            int total_sec = static_cast<int>(seconds);
            int m = total_sec / 60;
            int s = total_sec % 60;
            char buf[6];
            snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
            return buf;
        };

        std::string current_time_str = format_time(current_time);
        std::string total_time_str = format_time(total_time);
        std::string time_str = " " + current_time_str + " / " + total_time_str;

        std::string volume_str = "Vol: " + std::to_string(volume) + "% ";
        std::string status = is_paused ? "||" : "|>";

        int total_len = width;
        int status_start_pos = total_len / 2 - 1;

        std::string line(total_len, ' ');
        std::copy(time_str.begin(), time_str.end(), line.begin());
        std::copy(volume_str.begin(), volume_str.end(), line.end() - volume_str.size());
        if (status_start_pos >= 0 && status_start_pos + status.size() <= total_len)
            std::copy(status.begin(), status.end(), line.begin() + status_start_pos);

        buffer.append(line);
        buffer.push_back('\n');

        return buffer;
    }
}
