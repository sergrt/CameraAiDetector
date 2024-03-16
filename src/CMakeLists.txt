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
link_directories(${Boost_LIBRARY_DIRS})
# Suppress deprecation warnings - originated from TgBot library
add_definitions("-DBOOST_BIND_GLOBAL_PLACEHOLDERS")

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
    codeproject_ai_facade.cpp
    core.cpp
    error_reporter.cpp
    frame_reader.cpp
    hybrid_object_detect.cpp
    log.cpp
    main.cpp
    opencv_ai_facade.cpp
    settings.cpp
    simple_motion_detect.cpp
    telegram_bot.cpp
    video_writer.cpp)

set(HEADER
    ai.h
    ai_factory.h
    codeproject_ai_facade.h
    core.h
    error_reporter.h
    final_action.h
    frame_reader.h
    helpers.h
    hybrid_object_detect.h
    log.h
    opencv_ai_facade.h
    ring_buffer.h
    safe_ptr.h
    settings.h
    simple_motion_detect.h
    stream_properties.h
    telegram_bot.h
    telegram_messages.h
    translation.h
    uid_utils.h
    video_writer.h)

set(OTHER_FILES
    settings.json)

add_executable(${PROJECT_NAME} ${SOURCE} ${HEADER} ${OTHER_FILES})

IF (LANG_RU)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LANG_RU=${LANG_RU})
ENDIF()

IF (WIN32)
    IF (CMAKE_BUILD_TYPE STREQUAL "Release")
        SET(CURL_LIB_NAME libcurl_imp)
    ELSE()
        SET(CURL_LIB_NAME libcurl-d_imp)
    ENDIF()

    target_link_libraries(${PROJECT_NAME} TgBot ${CURL_LIB_NAME} OpenSSL::Crypto OpenSSL::SSL ${OpenCV_LIBS})
ELSE()
    # Sometimes it helps to add extra optimizations:
    # target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:RELEASE>:-Ofast>")
    target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:-Wall>")
    target_link_libraries(${PROJECT_NAME} TgBot CURL::libcurl OpenSSL::Crypto OpenSSL::SSL ${OpenCV_LIBS} boost_program_options tbb)
ENDIF()
