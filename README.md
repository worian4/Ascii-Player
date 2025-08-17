# Ascii-Player
An app to display videos as ASCII-video. The diffrence between this prokect and my ```scii-video``` is that this doesn't write the video, but it doesn't need to - it opens a video and displays its "ASCII version" directly, without pre-building video. Also i've added to it a "player" part, so it functions as a simple player, with simple ASCII interface. Now can run colored videos up to 300 symbols with 600 FPS in terminal.

Usage: when you're done with building, you'll have an ```ASCII_Player.exe``` file, so you'll have to choose a video, then choose "open with" and search for the file, and that's it. The first time it'll open with latency, so you'll have to wait some time.

![ancii_epic_test](https://github.com/user-attachments/assets/d9d49b21-b08a-430c-98b2-cb87902f9cbf)

Now both Windows and Linux supported*! Most of the files (except ```ascii-player.desktop``` and ```icon.rc```) are cross-platform, so to install you copy the same git and use almost the same files.

Installation for Windows:
1. Download C++ and C++ Build Tools for your system from VS Installer
2. Download opencv
3. Locate all the files from git
4. Download ```VLC``` and put ```plugins``` folder from ```Video LAN``` with all the files
5. Edit ```CMakeLists.txt``` with your opencv location
6. Build the project
7. Enjoy!


Installation for Linux:
1. Download C++
2. Download libraries via
```
sudo apt-get install build-essential cmake pkg-config libopencv-dev libopencv-core-dev libopencv-imgproc-dev libopencv-highgui-dev libvlc-dev libvlccore-dev vlc libncursesw5-dev
```
4. Dowload ```gcc```, ```g++``` and ```Ninja```
5. Download ```VLC```
6. Locate al the files from git
7. Build the project
8. Create a "shortcut" for the app via creating ```~/.local/share/applications/ascii-player.desktop``` with the file from git and choose your directiry to the build in in it
9. Enjoy!

*however, Linux version doesn't work as in Windows because of the terminal behaviour. Windows terminal allows to be resized and to be positioned from a program, which is not allowed on most of Linux terminals. So the diffrence is that you will have to set position and resize the window of the terminal manually, or just click 'maximize'.
