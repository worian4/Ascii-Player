#ifdef _WIN32
#include <windows.h>
void enableANSI() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}
#endif

#include <iostream>
#include <thread>
#include <atomic>
#include <windows.h>
#include <conio.h>
#include <opencv2/opencv.hpp>
#include "ascii_render.hpp"
using namespace ascii_render;

#include <queue>
#include <mutex>
#include <condition_variable>

// Типы для libvlc структур (просто void*)
using libvlc_instance_t = void;
using libvlc_media_t = void;
using libvlc_media_player_t = void;

// Правильные typedef (cdecl, как в libvlc)
using libvlc_new_t = libvlc_instance_t* (*)(int, const char* const*);
using libvlc_media_new_path_t = libvlc_media_t* (*)(libvlc_instance_t*, const char*);
using libvlc_media_player_new_from_media_t = libvlc_media_player_t* (*)(libvlc_media_t*);
using libvlc_media_release_t = void (*)(libvlc_media_t*);
using libvlc_audio_set_volume_t = void (*)(libvlc_media_player_t*, int);
using libvlc_media_player_play_t = int (*)(libvlc_media_player_t*);
using libvlc_media_player_set_pause_t = void (*)(libvlc_media_player_t*, int);
using libvlc_media_player_get_time_t = int64_t (*)(libvlc_media_player_t*);
using libvlc_media_player_get_length_t = int64_t (*)(libvlc_media_player_t*);
using libvlc_media_player_stop_t = void (*)(libvlc_media_player_t*);
using libvlc_media_player_release_t = void (*)(libvlc_media_player_t*);
using libvlc_release_t = void (*)(libvlc_instance_t*);

// Глобальные указатели на функции
libvlc_new_t libvlc_new = nullptr;
libvlc_media_new_path_t libvlc_media_new_path = nullptr;
libvlc_media_player_new_from_media_t libvlc_media_player_new_from_media = nullptr;
libvlc_media_release_t libvlc_media_release = nullptr;
libvlc_audio_set_volume_t libvlc_audio_set_volume = nullptr;
libvlc_media_player_play_t libvlc_media_player_play = nullptr;
libvlc_media_player_set_pause_t libvlc_media_player_set_pause = nullptr;
libvlc_media_player_get_time_t libvlc_media_player_get_time = nullptr;
libvlc_media_player_get_length_t libvlc_media_player_get_length = nullptr;
libvlc_media_player_stop_t libvlc_media_player_stop = nullptr;
libvlc_media_player_release_t libvlc_media_player_release = nullptr;
libvlc_release_t libvlc_release = nullptr;

// Загрузка функций из DLL
bool load_vlc_functions(HMODULE vlc) {
    libvlc_new = (libvlc_new_t)GetProcAddress(vlc, "libvlc_new");
    libvlc_media_new_path = (libvlc_media_new_path_t)GetProcAddress(vlc, "libvlc_media_new_path");
    libvlc_media_player_new_from_media = (libvlc_media_player_new_from_media_t)GetProcAddress(vlc, "libvlc_media_player_new_from_media");
    libvlc_media_release = (libvlc_media_release_t)GetProcAddress(vlc, "libvlc_media_release");
    libvlc_audio_set_volume = (libvlc_audio_set_volume_t)GetProcAddress(vlc, "libvlc_audio_set_volume");
    libvlc_media_player_play = (libvlc_media_player_play_t)GetProcAddress(vlc, "libvlc_media_player_play");
    libvlc_media_player_set_pause = (libvlc_media_player_set_pause_t)GetProcAddress(vlc, "libvlc_media_player_set_pause");
    libvlc_media_player_get_time = (libvlc_media_player_get_time_t)GetProcAddress(vlc, "libvlc_media_player_get_time");
    libvlc_media_player_get_length = (libvlc_media_player_get_length_t)GetProcAddress(vlc, "libvlc_media_player_get_length");
    libvlc_media_player_stop = (libvlc_media_player_stop_t)GetProcAddress(vlc, "libvlc_media_player_stop");
    libvlc_media_player_release = (libvlc_media_player_release_t)GetProcAddress(vlc, "libvlc_media_player_release");
    libvlc_release = (libvlc_release_t)GetProcAddress(vlc, "libvlc_release");

    return libvlc_new && libvlc_media_new_path && libvlc_media_player_new_from_media &&
           libvlc_media_release && libvlc_audio_set_volume && libvlc_media_player_play &&
           libvlc_media_player_set_pause && libvlc_media_player_get_time && libvlc_media_player_get_length &&
           libvlc_media_player_stop && libvlc_media_player_release && libvlc_release;
}

void set_console_size(int width, int height) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD bufferSize = { (SHORT)width, (SHORT)height };
    SMALL_RECT windowSize = { 0, 0, (SHORT)(width - 1), (SHORT)(height - 1) };
    SetConsoleScreenBufferSize(hOut, bufferSize);
    SetConsoleWindowInfo(hOut, TRUE, &windowSize);
}

void handle_input(libvlc_media_player_t* mediaPlayer,
                  std::atomic<bool>& running,
                  std::atomic<bool>& paused,
                  std::atomic<int>& volume)
{
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);

    INPUT_RECORD record;
    DWORD events;

    while (running.load()) {
        if (!ReadConsoleInput(hIn, &record, 1, &events))
            continue;

        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
            int vk = record.Event.KeyEvent.wVirtualKeyCode;
            if (vk == VK_ESCAPE) {
                running.store(false);
                break;
            } else if (vk == VK_SPACE) {
                bool new_paused = !paused.load();
                paused.store(new_paused);
                libvlc_media_player_set_pause(mediaPlayer, new_paused ? 1 : 0);
            } else if (vk == VK_UP) {
                int v = std::min(100, volume.load() + 5);
                volume.store(v);
                libvlc_audio_set_volume(mediaPlayer, v);
            } else if (vk == VK_DOWN) {
                int v = std::max(0, volume.load() - 5);
                volume.store(v);
                libvlc_audio_set_volume(mediaPlayer, v);
            }
        }
        else if (record.EventType == MOUSE_EVENT) {
            auto &me = record.Event.MouseEvent;
            if (me.dwEventFlags == MOUSE_WHEELED) {
                short delta = GET_WHEEL_DELTA_WPARAM(me.dwButtonState);
                if (delta > 0) { // колесо вверх
                    int v = std::min(100, volume.load() + 5);
                    volume.store(v);
                    libvlc_audio_set_volume(mediaPlayer, v);
                } else { // колесо вниз
                    int v = std::max(0, volume.load() - 5);
                    volume.store(v);
                    libvlc_audio_set_volume(mediaPlayer, v);
                }
            }
        }
    }
}

// Очередь и синхронизация для ASCII-строк
static std::queue<std::string> ascii_queue;
static std::mutex ascii_mutex;
static std::condition_variable ascii_cv;

// thread: читает кадры, преобразует в ASCII и кладёт в очередь
void video_processing_thread(cv::VideoCapture& cap, int width, int height,
                             libvlc_media_player_t* mediaPlayer,
                             std::atomic<bool>& running,
                             std::atomic<bool>& paused,
                             std::atomic<int>& volume,
                             double frame_duration)
{
    cv::Mat frame;
    cv::Mat last_frame;
    int frame_index = 0;
    double start_time = (double)cv::getTickCount() / cv::getTickFrequency();

    bool paused_frame_pushed = false;
    bool first_frame_read = false;

    while (running.load()) {
        bool paused_local = paused.load();

        if (!paused_local) {
            if (!cap.read(frame)) break;
            first_frame_read = true;
            cv::resize(frame, frame, cv::Size(width, height));
            last_frame = frame.clone();

            double current_time = 0.0;
            double duration = 0.0;
            if (libvlc_media_player_get_time) current_time = libvlc_media_player_get_time(mediaPlayer) / 1000.0;
            if (libvlc_media_player_get_length) duration = libvlc_media_player_get_length(mediaPlayer) / 1000.0;
            double progress = duration > 0 ? current_time / duration : 0.0;
            if (progress > 1.0) progress = 1.0;

            int volume_local = volume.load();

            std::string ascii = frame_to_ascii(frame, true, paused_local, progress, current_time, duration, volume_local);

            {
                std::lock_guard<std::mutex> lock(ascii_mutex);
                ascii_queue.push(std::move(ascii));
            }
            ascii_cv.notify_one();

            paused_frame_pushed = false;
        } else {
            if (first_frame_read && !paused_frame_pushed && !last_frame.empty()) {
                double current_time = 0.0;
                double duration = 0.0;
                if (libvlc_media_player_get_time) current_time = libvlc_media_player_get_time(mediaPlayer) / 1000.0;
                if (libvlc_media_player_get_length) duration = libvlc_media_player_get_length(mediaPlayer) / 1000.0;
                double progress = duration > 0 ? current_time / duration : 0.0;
                if (progress > 1.0) progress = 1.0;

                int volume_local = volume.load();

                std::string ascii = frame_to_ascii(last_frame, true, paused_local, progress, current_time, duration, volume_local);

                {
                    std::lock_guard<std::mutex> lock(ascii_mutex);
                    ascii_queue.push(std::move(ascii));
                }
                ascii_cv.notify_one();

                paused_frame_pushed = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }

        double next_time = start_time + frame_index * frame_duration;
        double now = (double)cv::getTickCount() / cv::getTickFrequency();
        double sleep_time = next_time - now;
        if (sleep_time > 0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
        }

        ++frame_index;
    }

    running.store(false);
    ascii_cv.notify_all();
}

// thread: берёт ASCII из очереди и отрисовывает
void render_thread(std::atomic<bool>& running)
{
    while (running.load() || !ascii_queue.empty()) {
        std::unique_lock<std::mutex> lock(ascii_mutex);
        ascii_cv.wait(lock, [&running]() { return !ascii_queue.empty() || !running.load(); });

        while (!ascii_queue.empty()) {
            std::string ascii = std::move(ascii_queue.front());
            ascii_queue.pop();
            lock.unlock();

            render_frame(ascii);

            lock.lock();
        }
    }
}

int main(int argc, char* argv[]) {
    #ifdef _WIN32
        freopen("nul", "w", stderr);
    #else
        freopen("/dev/null", "w", stderr);
    #endif

    if (argc < 2) {
        std::cerr << "Usage: program <video_path>" << std::endl;
        return 1;
    }

    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
    enableANSI();

    HMODULE vlc = LoadLibraryA("libvlc.dll");
    if (!vlc) {
        std::cerr << "Failed to load libvlc.dll\n";
        return 1;
    }

    if (!load_vlc_functions(vlc)) {
        std::cerr << "Failed to load one or more libvlc functions\n";
        FreeLibrary(vlc);
        return 1;
    }

    std::atomic<bool> running(true);
    std::atomic<bool> paused(false);
    std::atomic<int> volume(50);
    std::string video_path = argv[1];

    const char* vlc_args[] = {
        "--no-xlib",
        "--plugin-path=./plugins",
        "--no-video"
    };
    libvlc_instance_t* vlc_instance = libvlc_new(3, vlc_args);

    if (!vlc_instance) {
        std::cout << "Failed to create VLC instance";
        FreeLibrary(vlc);
        return 1;
    }

    libvlc_media_t* media = libvlc_media_new_path(vlc_instance, video_path.c_str());
    if (!media) {
        std::cerr << "Failed to create media for " << video_path << "\n";
        libvlc_release(vlc_instance);
        FreeLibrary(vlc);
        return 1;
    }

    libvlc_media_player_t* mediaPlayer = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);
    libvlc_audio_set_volume(mediaPlayer, volume.load());

    cv::VideoCapture cap(video_path);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open video file " << video_path << "\n";
        libvlc_media_player_release(mediaPlayer);
        libvlc_release(vlc_instance);
        FreeLibrary(vlc);
        return 1;
    }

    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0) {
        std::cerr << "Invalid FPS value: " << fps << "\n";
        libvlc_media_player_release(mediaPlayer);
        libvlc_release(vlc_instance);
        FreeLibrary(vlc);
        return 1;
    }
    double frame_duration = 1.0 / fps;

    int width = 120;
    int height = static_cast<int>((cap.get(cv::CAP_PROP_FRAME_HEIGHT) / cap.get(cv::CAP_PROP_FRAME_WIDTH)) * width * 0.55);
    set_console_size(width, height + 3);

    libvlc_media_player_play(mediaPlayer);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // input thread
    std::thread input_thread(handle_input, mediaPlayer, std::ref(running), std::ref(paused), std::ref(volume));

    // processing and rendering threads
    std::thread processing_thread(video_processing_thread, std::ref(cap), width, height,
                                  mediaPlayer, std::ref(running), std::ref(paused), std::ref(volume), frame_duration);

    std::thread drawing_thread(render_thread, std::ref(running));

    // Ждём завершения
    if (processing_thread.joinable()) processing_thread.join();
    if (drawing_thread.joinable()) drawing_thread.join();
    if (input_thread.joinable()) input_thread.join();

    libvlc_media_player_stop(mediaPlayer);
    libvlc_media_player_release(mediaPlayer);
    libvlc_release(vlc_instance);
    FreeLibrary(vlc);
    return 0;
}
