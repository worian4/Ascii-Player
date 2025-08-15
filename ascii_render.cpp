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



class ThreadPool {
public:
    explicit ThreadPool(int n) { reset(n); }
    ~ThreadPool() { stop(); }

    void reset(int n) {
        stop();
        if (n < 1) n = 1;
        stop_flag = false;
        for (int i = 0; i < n; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv_task.wait(lock, [&]{ return stop_flag || !tasks.empty(); });
                        if (stop_flag && tasks.empty()) return;
                        job = std::move(tasks.front()); tasks.pop();
                    }
                    job();
                }
            });
        }
    }

    void enqueue(std::function<void()> f) {
        { std::lock_guard<std::mutex> lock(mtx); tasks.push(std::move(f)); }
        cv_task.notify_one();
    }

    void job_started() { pending.fetch_add(1, std::memory_order_relaxed); }
    void job_finished() {
        if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lock(wait_mtx);
            cv_wait.notify_all();
        }
    }
    void wait_all() {
        std::unique_lock<std::mutex> lock(wait_mtx);
        cv_wait.wait(lock, [&]{ return pending.load(std::memory_order_acquire) == 0; });
    }

private:
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stop_flag = true;
        }
        cv_task.notify_all();
        for (auto& t : workers) if (t.joinable()) t.join();
        workers.clear();
        while (!tasks.empty()) tasks.pop();
        pending.store(0);
    }

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv_task;

    std::atomic<bool> stop_flag{false};
    std::atomic<int>  pending{0};
    std::mutex wait_mtx;
    std::condition_variable cv_wait;
};

// -------------------- ВСПОМОГАТЕЛЬНОЕ --------------------
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

// --- Утилиты ---
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
        static ThreadPool* pool = nullptr;
        static int pool_threads = 0;
        if (!pool) { pool = new ThreadPool(std::max(1,num_threads)); pool_threads = std::max(1,num_threads); }
        // фиксим число потоков, чтобы не пересоздавать каждый кадр
        const int T = std::max(1, pool_threads);

        CV_Assert(frame.type()==CV_8UC3 && frame.isContinuous());
        const int W = frame.cols, H = frame.rows;

        static const char* LUT =
            " `.,:;_-~\"><|!/)(^?}{][*=clsji+o2r7fC1xekJutFyVnLzS53TmG4PhaqEwYvZ96bdpg0OUAXNDQRKHM8B&%$W#@";
        const size_t LUTn = std::strlen(LUT);

        // --- разбиение строк между потоками ---
        const int rows_per = (H + T - 1) / T;

        // PASS 1: считаем точный объём вывода по каждой полосе
        std::vector<size_t> stripe_len(T, 0);

        for (int t=0; t<T; ++t){
            const int y0 = t*rows_per, y1 = std::min(H, y0+rows_per);
            if (y0>=H){ stripe_len[t]=0; continue; }
            pool->job_started();
            pool->enqueue([&,t,y0,y1]{
                size_t L = 0;
                for (int y=y0; y<y1; ++y){
                    const unsigned char* row = frame.ptr<unsigned char>(y);
                    int last_r=-1,last_g=-1,last_b=-1;
                    for (int x=0; x<W; ++x){
                        unsigned b=row[x*3+0], g=row[x*3+1], r=row[x*3+2];
                        if (r!=last_r || g!=last_g || b!=last_b){
                            L += ansi_len_rgb(r,g,b);
                            last_r=r; last_g=g; last_b=b;
                        }
                        L += 1; // сам символ
                    }
                    L += 1;   // '\n'
                }
                stripe_len[t]=L;
                pool->job_finished();
            });
        }
        pool->wait_all();

        // префикс-сумма → стартовые смещения каждого блока
        std::vector<size_t> offset(T, 0);
        size_t body_size = 0;
        for (int t=0; t<T; ++t){ offset[t]=body_size; body_size += stripe_len[t]; }

        // дополнительные хвосты кадра
        const size_t reset_len = 5;              // "\x1b[0m\n"
        const int barW = std::max(10, W);
        const size_t bar_len = barW + 1;         // + '\n'
        const size_t status_len = W + 1;         // + '\n'

        const size_t total_len = body_size + reset_len + bar_len + status_len;

        // общий буфер (один раз) → все потоки пишут в свои смещения
        std::vector<char> buf(total_len);
        char* base = buf.data();

        // PASS 2: запись в общий буфер по вычисленным смещениям
        for (int t=0; t<T; ++t){
            const int y0 = t*rows_per, y1 = std::min(H, y0+rows_per);
            if (y0>=H || stripe_len[t]==0) continue;

            pool->job_started();
            pool->enqueue([&,t,y0,y1]{
                char* p = base + offset[t];
                for (int y=y0; y<y1; ++y){
                    const unsigned char* row = frame.ptr<unsigned char>(y);
                    int last_r=-1,last_g=-1,last_b=-1;
                    for (int x=0; x<W; ++x){
                        unsigned b=row[x*3+0], g=row[x*3+1], r=row[x*3+2];
                        // яркость «на лету»
                        unsigned gray = (r*77u + g*150u + b*29u) >> 8;
                        char ch = LUT[(gray * (LUTn - 1)) / 255];

                        if (r!=last_r || g!=last_g || b!=last_b){
                            put_ansi_rgb(p, r,g,b);
                            last_r=r; last_g=g; last_b=b;
                        }
                        *p++ = ch;
                    }
                    *p++ = '\n';
                }
                // проверка: мы должны ровно уложиться в stripe_len[t]
                // (опционально можно assert)
                pool->job_finished();
            });
        }
        pool->wait_all();

        // Хвост кадра: один общий сброс цвета + прогресс-бар + статус-строка
        char* tail = base + body_size;
        std::memcpy(tail, "\x1b[0m\n", 5); tail += 5;

        // прогресс-бар
        double prog = std::clamp(progress, 0.0, 1.0);
        int filled = static_cast<int>(prog * barW);
        std::memset(tail, '#', filled);          tail += filled;
        std::memset(tail, '-', barW - filled);   tail += (barW - filled);
        *tail++ = '\n';

        // статус-строка
        auto put_time5 = [](double sec, char* out){
            int t = (int)sec; int m=t/60, s=t%60;
            out[0]='0'+(m/10); out[1]='0'+(m%10);
            out[2]=':'; out[3]='0'+(s/10); out[4]='0'+(s%10);
        };
        std::vector<char> line(W, ' ');
        char t1[5], t2[5]; put_time5(current_time,t1); put_time5(total_time,t2);

        // " mm:ss / mm:ss"
        const char prefix[] = " ";
        const char sep[]    = " / ";
        size_t pos = 0;
        line[pos++] = prefix[0];
        std::memcpy(&line[pos], t1, 5); pos+=5;
        std::memcpy(&line[pos], sep, 3); pos+=3;
        std::memcpy(&line[pos], t2, 5); pos+=5;

        std::string vol = "Vol: " + std::to_string(volume) + "% ";
        std::memcpy(&line[W - (int)vol.size()], vol.data(), vol.size());

        const char* status = is_paused ? "||" : "|>";
        int spos = W/2 - 1;
        if (spos>=0 && spos+2<=W) { line[spos]=status[0]; line[spos+1]=status[1]; }

        std::memcpy(tail, line.data(), W); tail += W;
        *tail++ = '\n';

        return  std::string(buf.data(), tail - buf.data());
    }
}
