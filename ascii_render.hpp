#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace ascii_render {

    size_t render_frame(const std::string& frame);

    std::string frame_to_ascii_mono(
        const cv::Mat& frame,
        bool is_paused,
        double progress,
        double current_time,
        double total_time,
        int volume
    );

    std::string apply_color_to_ascii(const cv::UMat& frame, const std::string& ascii);

}
