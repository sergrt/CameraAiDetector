# CameraAiDetector
AI-powered detection/notification system for cameras and video files

This application is based on YOLOv5 object detection engine, and performs the following:
- Capture video stream (from camera - e.g. **rtsp**, or any **video file** or source, supported by OpenCV)
- Detect objects using **AI** - persons, animals, vehicles, bicycles etc.
- Detect objects using **simple motion detection** (usable on low-end systems)
- Detect objects using **hybrid object detection** (something in between cpu/gpu-heavy AI and fast but not so accurate simple motion detection)
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
- Allow to use OpenCV DNN (with or without CUDA support) or CodeProject AI (https://www.codeproject.com/Articles/5322557/CodeProject-AI-Server-AI-the-easy-way)

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
1. Download latest package from the "Releases" section (or compile from sources) - Windows packages are available, linux compilation is rather simple and described in Compilation section
2. Prepare AI backend. Choose one and setup:
   1. Codeproject AI:
      - Download and install CodeProject AI from here: https://www.codeproject.com/Articles/5322557/CodeProject-AI-Server-AI-the-easy-way
      - Enable YOLOv5 from CodeProject AI dashboard - select one suitable for your platform
   2. OpenCV AI DNN:
      - Download YOLOv5 onnx file (see "Releases" section) - yolov5s is recommended
      - (For CUDA support) Download OpenCV_CUDA_libs (see "Releases" section) and replace application libs with CUDA-enabled libs
   3. Simple movement detection
      - No special prerequisites are required, but some tweaks of settings file might be necessary
3. Create new Telegram bot and obtain bot token
4. Update `settings.json` file. At least these parameters should be set:
   - `detection_engine` - selected engine. Valid values are:
     - `CodeprojectAI` - CodeProject AI engine
     - `OpenCV` - OpenCV engine
     - `Simple` - simple motion detection engine
     - `HybridCodeprojectAI` or `HybridOpenCV` - hybrid object detection. It uses simple motion detection, but calls specified AI backend when something appearing in the frame
   - `source` - video stream URL or video file path
   - `storage_path` - exisiting folder to store videos and images
   - `bot_token` - Telegram bot token
   - `allowed_users` - add yourself here
   - in case OpenCV AI DNN is preffered, set `use_codeproject_ai` to `false`, and point `onnx_file_path` to onnx file
5. Run app and  send `/start` to your bot

## AI backend notes
OpenCV DNN, CodeProject AI or Simple motion detection can be used to analyze video stream. Some notes to consider:
- OpenCV and CodeProject AI provide CUDA support, but using CodeProject AI does not require to build OpenCV with CUDA support
- OpenCV DNN uses approx. 20% less RAM than CodeProject AI
- Performance depends on hardware and operating system:
    - CUDA-enabled OpenCV and CodeProject AI seem to perform really close to each other
    - CPU calculations (on Linux systems) seem to perform better with OpenCV DNN. Benchmarking on low end mini-pc with Linux Ubuntu 22.04 shows that OpenCV DNN processes frame almost 20% faster
    - Simple motion detection is the fastest and is recommended for low-end systems
    - Hybrid object detection is quite easy on resources and provides more precise results than simple motion detection
- Compiling OpenCV with CUDA support is _really_ slow

## Configuration
Configuration is stored in `settings.json` file, and options are (mostly) self-explanatory. Some notes:
- `cooldown_write_time_ms` - time (in milliseconds) to write after object disappears
- `nth_detect_frame` - send every nth frame to AI. This helps to spare some system resources

Simple motion detection has some non-obvious settings:
- `gaussian_blur_sz` - part of image processing. The larger value the less smaller objects detected
- `threshold` - movement "heatmap" is processed based on this value. The larger value the less sensitive detection is
- `area_trigger` - size of objects to be detected. Larger value specifies more movement in frame. Useful for filtering out timestamps

Some low-end systems might benefit from tweaking `video_codec`. `mp4v` performs better than `avc1`, some other might be even faster.

To free up some resources on video encoding there's `decrease_detect_rate_while_writing` option. If set to `true` then the frames are checked less frequently when an alarm was triggered and video is being written.

NB: to tweak performance, try to use different frame scaling, and different image formats. These settings affect AI system and alarm notifications, but do not affect saved videos.

To use **multiple cameras** there's quick and dirty solution: command-line key `-c` (or `--config`) allows to set `settings.json` file by it's parameter. So it's possible to run several instances with different `source` variables.

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
Refer to [.github/workflows/cmake-multi-platform.yml](.github/workflows/cmake-multi-platform.yml) for complete set of commands.

NB: OpenCV compilation with CUDA support is not covered in the workflow file.

You may want to compile recent version of OpenCV to use with supplied onnx models, something like this:
```
$ git clone https://github.com/opencv/opencv_contrib
$ cmake -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local -D INSTALL_C_EXAMPLES=OFF -D INSTALL_PYTHON_EXAMPLES=OFF -D OPENCV_GENERATE_PKGCONFIG=ON -D BUILD_EXAMPLES=OFF -D OPENCV_EXTRA_MODULES_PATH=/path/to/opencv_contrib/modules ..
```

### Windows
Third-party dependencies could be quite tricky to install under Windows, so here is the fastest way:

#### Clone application repository and create 3rdparty dir:
With or without submodules. If cloned with `--recurse-submodules` there's no need to clone repos manually later:
```
$ git clone https://github.com/sergrt/CameraAiDetector.git --recurse-submodules
$ cd CameraAiDetector
$ mkdir 3rdparty
$ cd 3rdparty
```
#### Clone nlohmann-json
Skip if main repository was cloned with submodules.
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
Skip repo clone if main repository was cloned with submodules.
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

To compile with CUDA support some more steps needed, and different `cmake` command line is used:
1. Download and install NVidia CUDA
2. Download NVidia cudnn for the same CUDA version and unpack
```
# From 3rdparty dir:
# Clone opencv_contrib
$ git clone https://github.com/opencv/opencv_contrib
# From opencv/build dir:
$ cmake .. -DWITH_CUDA=ON -DOPENCV_DNN_CUDA=ON -DOPENCV_EXTRA_MODULES_PATH="path/to/opencv_contrib/modules" -DCUDNN_LIBRARY="path/to/cudnn/lib/x64/cudnn.lib" -DCUDNN_INCLUDE_DIR="path/to/cudnn/include"
# Build generated project with your compiler, e. g. Visual Studio or by calling cmake:
$ cmake --build . -j16 --config Release
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
