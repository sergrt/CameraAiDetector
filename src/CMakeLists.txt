cmake_minimum_required(VERSION 3.15)

project(CameraAiDetector)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

IF (WIN32)
  # Search prebuilt 3rd party dependencies in those folders
  list(APPEND CMAKE_PREFIX_PATH "${THIRDPARTY_DIR}/boost")
  list(APPEND CMAKE_PREFIX_PATH "${THIRDPARTY_DIR}/curl")
  set(CURL_LIBRARY "${THIRDPARTY_DIR}/curl/build/lib/${CMAKE_BUILD_TYPE}")
  list(APPEND CMAKE_PREFIX_PATH "${THIRDPARTY_DIR}/opencv/build")
  list(APPEND CMAKE_PREFIX_PATH "${THIRDPARTY_DIR}/openssl")
ELSE()
  list(APPEND CMAKE_PREFIX_PATH "${THIRDPARTY_DIR}")
ENDIF()

find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIR})

find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})
link_directories(${CURL_LIBRARIES})

include_directories("${THIRDPARTY_DIR}/json/include")

find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
link_directories(${OpenCV_LIBS})

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

include_directories(${OPENSSL_INCLUDE_DIR})

set(TGBOT_ROOT_DIR "${THIRDPARTY_DIR}/tgbot-cpp")
include_directories(${TGBOT_ROOT_DIR}/include)

IF (WIN32)
  link_directories(${TGBOT_ROOT_DIR}/build/${CMAKE_BUILD_TYPE})
ELSE()
  link_directories(${TGBOT_ROOT_DIR}/build)
ENDIF()

set(SOURCE
  CodeprojectAiFacade.cpp
  Core.cpp
  FrameReader.cpp
  Log.cpp
  main.cpp
  Settings.cpp
  TelegramBot.cpp
  VideoWriter.cpp)

set(HEADER
  CodeprojectAiFacade.h
  Core.h
  ErrorReportState.h
  FinalAction.h
  FrameReader.h
  Helpers.h
  Log.h
  Settings.h
  StreamProperties.h
  TelegramBot.h
  Translation.h
  UidUtils.h
  VideoWriter.h)

add_executable(${PROJECT_NAME} ${SOURCE} ${HEADER})

IF (WIN32)
  IF (CMAKE_BUILD_TYPE STREQUAL "Release")
    SET(CURL_LIB_NAME libcurl_imp)
  ELSE()
    SET(CURL_LIB_NAME libcurl-d_imp)
  ENDIF()

  target_link_libraries(${PROJECT_NAME} TgBot ${CURL_LIB_NAME} OpenSSL::Crypto OpenSSL::SSL ${OpenCV_LIBS})
ELSE()
  # target_link_directories(VideoAiDetector PUBLIC "${TGBOT_ROOT_DIR}/build")
  target_link_libraries(${PROJECT_NAME} TgBot CURL::libcurl OpenSSL::Crypto OpenSSL::SSL ${OpenCV_LIBS})
ENDIF()
