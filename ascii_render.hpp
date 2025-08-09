#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace ascii_render {

    size_t render_frame(const std::string& frame);

    std::string frame_to_ascii(
        const cv::Mat& frame,
        bool use_color = false,
        bool is_paused = false,
        double progress = 0.0,
        double current_time = 0.0,
        double total_time = 0.0,
        int volume = 50
    );

}

