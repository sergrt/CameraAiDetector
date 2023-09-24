# CameraAiDetector
AI-powered detection/notification system for cameras and video files

This application is based on CodeProject AI (https://www.codeproject.com/Articles/5322557/CodeProject-AI-Server-AI-the-easy-way), and performs the following:
- Capture video stream (from camera - e.g. **rtsp**, or any **video file** or source, supported by OpenCV)
- Detect objects using AI - persons, animals, vehicles, bicycles etc.
- Send notifications via Telegram
- Save video based on detection
- Serve some handy Telegram requests - on-demand images, video download etc.

The application is written using C++, so it can be compiled on any supported platform.

## Features
- Send telegram notifications to authorized users about objects on camera (alarm images)
  
  <img src="../media/person.jpg" alt="drawing" width="300"/> <img src="../media/cars.jpg" alt="drawing" width="300"/>
- Record videos based on detected object - useful to save space on device (only videos of interest are recorded)
- Send image with video preview after the video has been recorded - several frames to see if something interesting was recorded
  
  <img src="../media/video_preview.jpg" alt="drawing" width="300"/>
- Allow users to get instant shots from camera
- List all recorded videos - with or without previews, optionally filtered by time depth
- Allow users to download particular video

## Telegram bot commands
- `/start` - start using bot - main menu
- `/image` - get instant shot from camera
- `/videos` - get list of recorded videos
- `/previews` - get list of recorded videos with previews
- `/video_<id>` - get video with `<id>`
- `/ping` - check app is up and running - report current time and free disk space
- `/log` - get log tail, useful to check what's going on

List of videos (and previews) can be filtered by time depth. For example, use `/videos 30m` to get list of videos recorded for last 30 minutes. Supported suffixes are: `m` (minutes), `h` (hours) and `d` (days).

## Installation
1. Download latest package from the "Releases" section
2. Download and install CodeProject AI from here: https://www.codeproject.com/Articles/5322557/CodeProject-AI-Server-AI-the-easy-way
3. Enable YOLOv5 from CodeProject AI dashboard - select one suitable for your platform
4. Create new Telegram bot and obtain bot token
5. Update `settings.json` file. At least these parameters should be set:
   - `source` - video stream URL or video file path
   - `storage_path` - exisiting folder to store videos and images
   - `bot_token` - Telegram bot token
   - `allowed_users` - add yourself here
6. Run app and  send `/start` to your bot

## Configuration
Configuration is stored in `settings.json` file, and options are (mostly) self-explanatory. Some notes:
- `cooldown_write_time_ms` - time (in milliseconds) to write after object disappears
- `nth_detect_frame` - send every nth frame to AI. This helps to spare some system resources

NB: to tweak performance, try to use different frame scaling, and different image formats. These settings affect AI system and alarm notifications, but do not affect saved videos.

## Compilation
### Requirements:
- C++20 compatible compiler (see notes on how to use it with older compilers), so gcc 13 or modern Visual Studio is required
- CMake
- 3rd party libs:
  - Boost
  - nlohmann-json
  - CURL
  - OpenCV
  - tgbot-cpp - and it has it's own dependencies:
    - OpenSSL
    - Boost
    - ZLib

### Linux
Compilation for Linux is quite straightforward - any dependencies could be installed by distro packet manager, so just use cmake and make. The only thing that requires attention is tgbot-cpp (https://github.com/reo7sp/tgbot-cpp), with newer boost libraries it requires modification of it's `CMakeLists.txt`, see Windows installation section.

### Windows
Third-party dependencies could be quite tricky to install under Windows, so here is the fastest way:

#### Clone application repository and create 3rdparty dir:
```
$ git clone https://github.com/sergrt/CameraAiDetector.git
$ cd CameraAiDetector
$ mkdir 3rdparty
$ cd 3rdparty
```
#### Clone nlohmann-json
```
# From 3rdparty dir:
$ git clone https://github.com/nlohmann/json.git
```
#### Compile CURL
```
# From 3rdparty dir:
$ git clone https://github.com/curl/curl.git
$ cd curl
$ mkdir build
$ cd build
$ cmake ..
# Build generated project with your compiler, e. g. Visual Studio
```
#### Compile OpenSSL
Install Perl, for example, Strawberry Perl (https://strawberryperl.com/)
```
# From 3rdparty dir:
$ git clone https://github.com/openssl/openssl.git
```
(only for Visual Studio build) Open Developer Command Prompt and cd to openssl dir
```
$ perl Configure VC-WIN64A no-asm no-shared
$ nmake.exe -f makefile
```
#### Compile Boost
Download boost lib from boost.org
Unpack into `3rdparty` (you should have file tree like this: `3rdparty/boost/bootstrap.bat`)
```
# From 3rdparty/boost dir:
$ ./bootstrap.bat
$ ./b2
```
#### Compile ZLib
```
# From 3rdparty dir:
$ git clone https://github.com/madler/zlib.git
$ cd zlib
$ mkdir build
$ cmake ..
# Build generated project with your compiler, e. g. Visual Studio
```
#### Compile tgbot-cpp
```
# From 3rdparty dir:
$ git clone https://github.com/reo7sp/tgbot-cpp.git
```
If you use recent versions of boost, then you need to remove `COMPONENTS system` from requirements:
Edit file `tgbot-cpp/CMakeLists.txt` and replace `find_package(Boost 1.65.1 COMPONENTS system REQUIRED)` with `find_package(Boost 1.65.1 REQUIRED)`
```
$ cmake -DZLIB_INCLUDE_DIR="c:\workspace\CameraAiDetector\3rdparty\zlib\" -DZLIB_LIBRARY="c:\workspace\CameraAiDetector\3rdparty\zlib\build\Debug\zlibd.lib" -DOPENSSL_ROOT_DIR="c:\workspace\CameraAiDetector\3rdparty\openssl\" -DBoost_INCLUDE_DIR="c:\workspace\CameraAiDetector\3rdparty\boost\" ..
# Build generated project with your compiler, e. g. Visual Studio
```
#### Compile OpenCV
```
# From 3rdparty dir:
$ git clone https://github.com/opencv/opencv.git
$ cd opencv
$ mkdir build
$ cd build
$ cmake ..
# Build generated project with your compiler, e. g. Visual Studio
```
#### Build application:
```
# From CameraAiDetector dir:
$ mkdir build
$ cd build
$ cmake ..
# Build generated project with your compiler, e. g. Visual Studio
```
#### Notes
For older compilers you need to alter code. Some things to consider:
- replace `jthread` with `thread` (and uncomment some code to `join()` them on application stop)
- string formatting should be rewritten
- use older stream instead of syncronized stream
- remove `zoned_time` and use utc
- something should be done with `std::filesystem`

