# CameraAiDetector
Camera and video AI detection/notification system

This application is based on CodeProject AI (https://www.codeproject.com/Articles/5322557/CodeProject-AI-Server-AI-the-easy-way), and performs the following:
- Capture video stream (from camera - e. g. **rtsp**, or any video file or source, supported by OpenCV)
- Detect objects using AI - persons, animals, vehicles, bicycles etc.
- Send notifications via Telegram
- Save video based on detection
- Serve some handy Telegram requests - on-demand images, video download etc.

The application is written using C++, so it can be compiled on any supported platform.
### Features
- Notify Telegram bot authorized users about objects on camera (alarm images)
- Record videos based on detected object - useful to save space on device (only videos of interest are recorded)
- Send image with video preview after the video has been recorded
- Allow users to get instant shot from camera
- List all recorded videos
- Allow users to download particular video

### Telegram bot commands
- `/start` - start using bot
- `/image` - get instant shot from camera
- `/list_videos` - get list of recorded videos
- `/video_<name>` - get video with `<name>`
- `/ping` - check app is up and running

### Requirements:
- C++20 compatible compiler - application uses `jthread`, `syncstream` and `format` (see notes on how to use it with older compilers), so gcc 13 or modern Visual Studio is required
- CMake
- 3rd party libs:
  - nlohmann-json
  - CURL
  - OpenCV
  - tgbot-cpp - and it has it's own dependencies:
    - OpenSSL
    - Boost
    - ZLib

## Configuration
Configuration is stored in `settings.json` file, and options are (mostly) self-explainatory. Some notes:
- `cooldown_write_time_ms` - time (in milliseconds) to write after object disappears
- `nth_detect_frame` - send every nth frame to AI. This helps to spare some system resources

To tweak performance, try to use different frame scaling, and different image formats. These settings affect AI system and alarm notifications, but do not affect saved videos.

## Compilation
### Linux
Linux compilation is quite straightforward - any dependencies could be installed by distro packet manager, so just use cmake and make.
### Window
Third-party dependencies could be quite tricky to install under Windows, so here is the fastest way:

#### Install CodeProject AI server
Download here: https://www.codeproject.com/Articles/5322557/CodeProject-AI-Server-AI-the-easy-way

#### Clone application repositiory and create 3rdparty dir:
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
Unpack into `3rdparty` (you should have filetree like this: `3rdparty/boost/bootstrap.bat`)
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
$ cmake -DZLIB_INCLUDE_DIR="c:\workspace\CameraAiDetector\3rdparty\zlib\" -DZLIB_LIBRARY="c:\workspace\CameraAiDetector\3rdparty\zlib\build\Debug\" -DOPENSSL_ROOT_DIR="c:\workspace\CameraAiDetector\3rdparty\openssl\" -DBoost_INCLUDE_DIR="c:\workspace\CameraAiDetector\3rdparty\boost\" ..
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
## Notes
For older compilers tou need to alter code. Replace `jthread` with `thread` (and uncomment some code to `join()` them on application stop), replace formatting and use older streams.
