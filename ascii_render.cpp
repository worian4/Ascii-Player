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

    std::string apply_color_to_ascii(const cv::Mat& frame, const std::string& ascii) {
        std::ostringstream colored;
        int width = frame.cols;
        int height = frame.rows;
        int x = 0, y = 0;

        for (char ch : ascii) {
            if (ch == '\r') continue;
            if (ch == '\n') {
                colored << "\x1b[0m\n";
                x = 0;
                y++;
                continue;
            }

            if (x >= 0 && x < width && y >= 0 && y < height) {
                cv::Vec3b bgr;
                if (frame.channels() == 3) {
                    bgr = frame.at<cv::Vec3b>(y, x);
                } else {
                    cv::Vec4b bgra = frame.at<cv::Vec4b>(y, x);
                    bgr = cv::Vec3b(bgra[0], bgra[1], bgra[2]);
                }
                colored << "\x1b[38;2;" 
                        << (int)bgr[2] << ";" 
                        << (int)bgr[1] << ";" 
                        << (int)bgr[0] << "m" 
                        << ch;
            } else {
                colored << ch;
            }

            ++x;
        }
        colored << "\x1b[0m";
        return colored.str();
    }
}
