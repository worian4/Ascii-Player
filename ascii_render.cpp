#include "ascii_render.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cstring>

namespace ascii_render {

    static std::vector<std::string> prev_lines;

    size_t render_frame(const std::string& frame) {
        std::vector<std::string> lines;
        size_t pos = 0, prev = 0;
        while ((pos = frame.find('\n', prev)) != std::string::npos) {
            lines.push_back(frame.substr(prev, pos - prev));
            prev = pos + 1;
        }
        lines.push_back(frame.substr(prev));

        std::cout << "\x1b[H";

        size_t updated_lines = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            bool different = (i >= prev_lines.size() || lines[i] != prev_lines[i]);
            if (different) {
                std::cout << "\x1b[" << (i + 1) << ";1H" << lines[i];
                ++updated_lines;
            }
        }
        std::cout.flush();
        prev_lines = lines;

        return updated_lines;
    }

    std::string frame_to_ascii(
        const cv::Mat& frame,
        bool use_color,
        bool is_paused,
        double progress,
        double current_time,
        double total_time,
        int volume
    ) {
        if (frame.channels() != 3) {
            throw std::runtime_error("Expected 3-channel RGB image");
        }

        int height = frame.rows;
        int width = frame.cols;

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_RGB2GRAY);

        std::ostringstream oss;

        auto gray_to_ascii = [](uchar value) -> char {
            static const char* chars = " `.,:;_-~\"><|!/)(^?}{][*=clsji+o2r7fC1xekJutFyVnLzS53TmG4PhaqEwYvZ96bdpg0OUAXNDQRKHM8B&%$W#@";
            size_t len = strlen(chars);
            int index = static_cast<int>((value * (len - 1)) / 255);
            return chars[index];
        };

        for (int y = 0; y < gray.rows; ++y) {
            for (int x = 0; x < gray.cols; ++x) {
                cv::Vec3b rgb = frame.at<cv::Vec3b>(y, x);
                uchar r = rgb[0];
                uchar g = rgb[1];
                uchar b = rgb[2];

                uchar pixel = gray.at<uchar>(y, x);
                char ch = gray_to_ascii(pixel);

                if (use_color) {
                    oss << "\x1b[38;2;" << (int)r << ";" << (int)g << ";" << (int)b << "m" << ch;
                } else {
                    oss << ch;
                }
            }
            oss << "\x1b[0m\n";
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
        if (use_color) {
            status = "\x1b[38;2;255;255;0m" + status + "\x1b[0m";
        }

        int bar_width = width;
        if (bar_width < 10) bar_width = 10;

        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;

        int filled_length = static_cast<int>(progress * bar_width);
        char fill_char = '#';
        char empty_char = '-';
        std::string progress_bar = std::string(filled_length, fill_char) + std::string(bar_width - filled_length, empty_char);

        oss << progress_bar << "\n";

        std::string volume_str = "Vol: " + std::to_string(volume) + "% ";

        int total_len = width;
        int time_len = (int)time_str.length();
        int status_len = 2; // длина статуса ("||" или "|>")
        int volume_len = (int)volume_str.length();

        // Позиция, куда вставлять статус, чтобы он был ровно по центру всей строки
        int status_start_pos = total_len / 2 - status_len / 2;

        // Создаем строку из пробелов длиной total_len
        std::string line(total_len, ' ');

        // Вставляем время слева
        for (int i = 0; i < time_len && i < total_len; ++i) {
            line[i] = time_str[i];
        }

        // Вставляем громкость справа
        for (int i = 0; i < volume_len && (total_len - volume_len + i) < total_len; ++i) {
            line[total_len - volume_len + i] = volume_str[i];
        }

        // Вставляем статус в центр строки
        // Статус может содержать ANSI цвета — чтобы цвета сохранить, соберем строку по частям:

        std::ostringstream oss_line;

        // Левая часть (до статуса)
        oss_line << line.substr(0, status_start_pos);

        // Цветной статус
        std::string raw_status = is_paused ? "||" : "|>";
        std::string colored_status = raw_status;
        if (use_color) {
            colored_status = "\x1b[38;2;255;255;0m" + raw_status + "\x1b[0m";
        }
        oss_line << colored_status;

        // Правая часть (после статуса)
        oss_line << line.substr(status_start_pos + status_len);

        oss << oss_line.str() << "\n";

        return oss.str();
    }

} // namespace ascii_render
