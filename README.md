# CameraAiDetector
Camera and video AI detection/notification system


__draft__

windows build:




mkdir 3rdparty
cd 3rdparty

git clone https://github.com/nlohmann/json.git


git clone https://github.com/curl/curl.git 

cd curl
mkdir build
cd build
cmake ..

# build

cd ../../
git clone https://github.com/openssl/openssl.git
https://strawberryperl.com/
Developer prompt

cd openssl
perl Configure VC-WIN64A no-asm no-shared
nmake.exe -f makefile
this might take a while

boost.org
unpack to 3rdparty - 3rdparty/boost/bootstrap.bat
.\bootstrap.bat
.\b2

git clone https://github.com/madler/zlib.git
cd zlib
mkdir build
cmake ..
#build with vs


git clone https://github.com/reo7sp/tgbot-cpp.git
find_package(Boost 1.65.1 COMPONENTS system REQUIRED)
cmake -DZLIB_INCLUDE_DIR="c:\workspace\CameraAiDetector\3rdparty\zlib\" -DZLIB_LIBRARY="c:\workspace\CameraAiDetector\3rdparty\zlib\build\Debug\" -DOPENSSL_ROOT_DIR="c:\workspace\CameraAiDetector\3rdparty\openssl\" -DBoost_INCLUDE_DIR="c:\workspace\CameraAiDetector\3rdparty\boost\" ..
#build

git clone https://github.com/opencv/opencv.git
cd opencv
mkdir build
cd build
cmake ..
#build with vs


mkdir build
cd build
cmake ..