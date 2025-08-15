#include "ascii_render.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <vector>
#include <opencv2/opencv.hpp>
#include <immintrin.h>
#include <thread>
#include <queue>
#include <functional>
#include <condition_variable>
#include <atomic>
#include <numeric>
#include <array>
#include <algorithm>



class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop_flag(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->mtx);
                        cv_task.wait(lock, [this] {
                            return this->stop_flag || !this->tasks.empty();
                        });
                        if (this->stop_flag && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            stop_flag = true;
        }
        cv_task.notify_all();
        for (auto &t : workers) t.join();
    }
    void enqueue(std::function<void()> f) {
        {
            std::unique_lock<std::mutex> lock(mtx);
            tasks.push(std::move(f));
        }
        cv_task.notify_one();
    }
    void wait_all() {
        std::unique_lock<std::mutex> lock(wait_mtx);
        cv_wait.wait(lock, [this] { return pending_jobs == 0; });
    }
    void job_started() {
        pending_jobs++;
    }
    void job_finished() {
        {
            std::unique_lock<std::mutex> lock(wait_mtx);
            pending_jobs--;
        }
        cv_wait.notify_all();
    }
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv_task;
    std::atomic<bool> stop_flag;

    std::mutex wait_mtx;
    std::condition_variable cv_wait;
    std::atomic<int> pending_jobs{0};
};

static constexpr int Q_LEVELS = 6;
static constexpr int Q_COUNT  = Q_LEVELS*Q_LEVELS*Q_LEVELS;

static std::array<std::array<char, 20>, Q_COUNT> g_ansi; 
static std::array<uint8_t, Q_COUNT>              g_ansi_len{};

static inline void ansi_init_once() {
    static bool inited = false;
    if (inited) return;

    auto lvl = [](int i){ return (i * 255) / (Q_LEVELS - 1); }; // i ∈ [0..5]
    for (int r = 0; r < Q_LEVELS; ++r) {
        for (int g = 0; g < Q_LEVELS; ++g) {
            for (int b = 0; b < Q_LEVELS; ++b) {
                int idx = (r * Q_LEVELS + g) * Q_LEVELS + b;
                int R = lvl(r), G = lvl(g), B = lvl(b);
                char* p = g_ansi[idx].data();
                // "\x1b[38;2;R;G;Bm"
                std::memcpy(p, "\x1b[38;2;", 7); p += 7;
                p += std::sprintf(p, "%d;%d;%dm", R, G, B);
                g_ansi_len[idx] = static_cast<uint8_t>(p - g_ansi[idx].data());
            }
        }
    }
    inited = true;
}

static inline int qidx(unsigned v) {
    return (int)((v * Q_LEVELS) >> 8);
}

static inline int digits_u8(unsigned v){ return (v>=100)?3: (v>=10)?2:1; }

static inline void put_u8(char*& p, unsigned v){
    if (v>=100){ *p++='0'+(v/100); v%=100; *p++='0'+(v/10); *p++='0'+(v%10); }
    else if(v>=10){ *p++='0'+(v/10); *p++='0'+(v%10); }
    else { *p++='0'+v; }
}

static inline int ansi_len_rgb(unsigned r,unsigned g,unsigned b){
    // "\x1b[38;2;" + r + ';' + g + ';' + b + 'm'
    return 7 + digits_u8(r) + 1 + digits_u8(g) + 1 + digits_u8(b) + 1;
}
static inline void put_ansi_rgb(char*& p, unsigned r,unsigned g,unsigned b){
    std::memcpy(p, "\x1b[38;2;", 7); p+=7;
    put_u8(p,r); *p++=';'; put_u8(p,g); *p++=';'; put_u8(p,b); *p++='m';
}

static inline void append_u8(char*& ptr, unsigned v) {
    if (v >= 100) {
        *ptr++ = char('0' + (v / 100));
        v %= 100;
        *ptr++ = char('0' + (v / 10));
        *ptr++ = char('0' + (v % 10));
    } else if (v >= 10) {
        *ptr++ = char('0' + (v / 10));
        *ptr++ = char('0' + (v % 10));
    } else {
        *ptr++ = char('0' + v);
    }
}
static inline void append_ansi_rgb(char*& ptr, unsigned r, unsigned g, unsigned b) {
    memcpy(ptr, "\x1b[38;2;", 7); ptr += 7;
    append_u8(ptr, r); *ptr++ = ';';
    append_u8(ptr, g); *ptr++ = ';';
    append_u8(ptr, b); *ptr++ = 'm';
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
        const cv::Mat& frame,   // CV_8UC3, continuous
        bool is_paused,
        double progress,
        double current_time,
        double total_time,
        int volume,
        int num_threads
    ){
        ansi_init_once();

        static ThreadPool* pool = nullptr;
        static int pool_threads = 0;
        if (!pool) { pool = new ThreadPool(std::max(1, num_threads)); pool_threads = std::max(1, num_threads); }
        const int T = std::max(1, pool_threads);

        CV_Assert(frame.type()==CV_8UC3 && frame.isContinuous());
        const int W = frame.cols, H = frame.rows;

        static const char* LUT =
            " `.,:;_-~\"><|!/)(^?}{][*=clsji+o2r7fC1xekJutFyVnLzS53TmG4PhaqEwYvZ96bdpg0OUAXNDQRKHM8B&%$W#@";
        const size_t LUTn = std::strlen(LUT);

        const int rows_per = (H + T - 1) / T;

        std::vector<size_t> blk_len(T, 0);
        for (int t = 0; t < T; ++t) {
            int y0 = t * rows_per, y1 = std::min(H, y0 + rows_per);
            if (y0 >= H) continue;
            pool->job_started();
            pool->enqueue([&, t, y0, y1] {
                size_t L = 0;
                for (int y = y0; y < y1; ++y) {
                    const unsigned char* row = frame.ptr<unsigned char>(y);
                    int last_idx = -1;
                    for (int x = 0; x < W; ++x) {
                        unsigned b = row[x*3+0], g = row[x*3+1], r = row[x*3+2];
                        int idx = (qidx(r) * Q_LEVELS + qidx(g)) * Q_LEVELS + qidx(b);
                        if (idx != last_idx) { L += g_ansi_len[idx]; last_idx = idx; }
                        L += 1;
                    }
                    L += 1; // '\n'
                }
                blk_len[t] = L;
                pool->job_finished();
            });
        }
        pool->wait_all();

        std::vector<size_t> offset(T, 0);
        size_t body = 0;
        for (int t = 0; t < T; ++t) { offset[t] = body; body += blk_len[t]; }

        const size_t reset_len = 5;               // "\x1b[0m\n"
        const int    barW      = std::max(10, W);
        const size_t bar_len   = barW + 1;        // + '\n'
        const size_t status_len= W + 1;           // + '\n'
        const size_t total_len = body + reset_len + bar_len + status_len;

        std::vector<char> buf(total_len);
        char* base = buf.data();

        for (int t = 0; t < T; ++t) {
            int y0 = t * rows_per, y1 = std::min(H, y0 + rows_per);
            if (y0 >= H || blk_len[t] == 0) continue;
            pool->job_started();
            pool->enqueue([&, t, y0, y1] {
                char* p = base + offset[t];
                for (int y = y0; y < y1; ++y) {
                    const unsigned char* row = frame.ptr<unsigned char>(y);
                    int last_idx = -1;
                    for (int x = 0; x < W; ++x) {
                        unsigned b = row[x*3+0], g = row[x*3+1], r = row[x*3+2];

                        int idx = (qidx(r) * Q_LEVELS + qidx(g)) * Q_LEVELS + qidx(b);
                        if (idx != last_idx) {
                            std::memcpy(p, g_ansi[idx].data(), g_ansi_len[idx]);
                            p += g_ansi_len[idx];
                            last_idx = idx;
                        }

                        unsigned gray = (r*77u + g*150u + b*29u) >> 8;
                        *p++ = LUT[(gray * (LUTn - 1)) / 255];
                    }
                    *p++ = '\n';
                }
                pool->job_finished();
            });
        }
        pool->wait_all();

        char* tail = base + body;
        std::memcpy(tail, "\x1b[0m\n", 5); tail += 5;

        double prog = std::clamp(progress, 0.0, 1.0);
        int filled = static_cast<int>(prog * barW);
        std::memset(tail, '#', filled);            tail += filled;
        std::memset(tail, '-', barW - filled);     tail += (barW - filled);
        *tail++ = '\n';

        auto put_time5 = [](double sec, char* out){
            int t = (int)sec; int m = t/60, s = t%60;
            out[0]='0'+(m/10); out[1]='0'+(m%10);
            out[2]=':'; out[3]='0'+(s/10); out[4]='0'+(s%10);
        };
        std::vector<char> line(W, ' ');
        char t1[5], t2[5]; put_time5(current_time,t1); put_time5(total_time,t2);

        // " mm:ss / mm:ss"
        size_t pos = 0;
        line[pos++] = ' ';
        std::memcpy(&line[pos], t1, 5); pos += 5;
        std::memcpy(&line[pos], " / ", 3); pos += 3;
        std::memcpy(&line[pos], t2, 5); pos += 5;

        std::string vol = "Vol: " + std::to_string(volume) + "% ";
        std::memcpy(&line[W - (int)vol.size()], vol.data(), vol.size());

        const char* status = is_paused ? "||" : "|>";
        int spos = W/2 - 1;
        if (spos >= 0 && spos + 2 <= W) { line[spos]=status[0]; line[spos+1]=status[1]; }

        std::memcpy(tail, line.data(), W); tail += W;
        *tail++ = '\n';

        return std::string(buf.data(), tail - buf.data());
    }
}

