#pragma once

#include "log.h"

#include <boost/regex.hpp>

#include <array>
#include <filesystem>
#include <vector>

constexpr int MaxFileSizeBytes = 40'000'000;

inline std::string exec(const std::string& cmd) {
    LOG_DEBUG << "Executing command: " << cmd;

    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, void(*)(FILE*)> pipe(popen(cmd.c_str(), "r"),
    [](FILE * f) -> void {
        std::ignore = pclose(f);
    });

    if (!pipe) {
        LOG_ERROR << "popen() failed!";
        return {};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    LOG_DEBUG << "Execution command output: " << result;
    return result;
}

inline float GetFileDuration(const std::filesystem::path& file_path) {
    auto duration_str = exec("ffprobe -i " + file_path.generic_string() + " -show_entries format=duration -v quiet -of default=noprint_wrappers=1:nokey=1");
    float duration = std::stof(duration_str);
    LOG_DEBUG << "Duration: " << duration;
    return duration;
}

inline std::string GetVideoCodec(const std::filesystem::path& file_path) {
    auto codec_str = exec("ffprobe -i " + file_path.generic_string() + " -select_streams v:0 -show_entries stream=codec_name -v quiet -of default=noprint_wrappers=1:nokey=1");
    codec_str.erase(std::remove_if(codec_str.begin(), codec_str.end(), ::isspace), codec_str.end());
    LOG_DEBUG << "Video codec: " << codec_str;
    return codec_str;
}

inline std::string GetAudioCodec(const std::filesystem::path& file_path) {
    auto codec_str = exec("ffprobe -i " + file_path.generic_string() + " -select_streams a:0 -show_entries stream=codec_name -v quiet -of default=noprint_wrappers=1:nokey=1");
    codec_str.erase(std::remove_if(codec_str.begin(), codec_str.end(), ::isspace), codec_str.end());
    LOG_DEBUG << "Audio codec: " << codec_str;
    return codec_str;
}

inline std::vector<std::filesystem::path> SplitVideoFile(const std::filesystem::path& file_path) {
    const auto file_name = file_path.filename().generic_string();
    const auto file_name_no_ext = file_path.stem().generic_string();

    LOG_DEBUG << "Splitting file: " << file_name;
    // Get file duration
    double duration = GetFileDuration(file_path);
    float cur_duration = 0.0f;
    size_t part_number = 0;
    const auto ext = file_path.extension().generic_string();

    std::vector<std::filesystem::path> res;

    while (cur_duration < duration) {
        ++part_number;
        std::string part_file_name = file_name_no_ext + "_part" + std::to_string(part_number) + ext;
        LOG_DEBUG << "Part file name: " << part_file_name;
        std::filesystem::path part_file_name_full_path = file_path.parent_path() / part_file_name;
        std::string cmd = "ffmpeg -i " + file_path.generic_string() + " -ss " + std::to_string(cur_duration) + " -fs " + std::to_string(MaxFileSizeBytes) 
            //+ " -c copy " // Produces unstable files
            + " -vcodec " + GetVideoCodec(file_path) + " "
            + " -acodec " + GetAudioCodec(file_path) + " "
            + part_file_name_full_path.generic_string();
        exec(cmd);

        cur_duration += GetFileDuration(part_file_name_full_path);
        LOG_DEBUG << "New start position: " << cur_duration;
        res.push_back(part_file_name_full_path);
        if (duration - cur_duration <= 2.0f) {
            // Disable ffmpeg to write empty files
            break;
        }
    }
    LOG_DEBUG << "Done splitting file";
    return res;
}

inline std::vector<std::filesystem::path> EnumerateByMask(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> result;
    
    const boost::regex regex(".*" + path.filename().stem().generic_string() + R"(_part\d{1,2}\..*)");
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
        if (!std::filesystem::is_regular_file(entry))
            continue;

        if (boost::regex_match(entry.path().filename().generic_string(), regex)) {
            LOG_DEBUG << "Found file: " << entry.path();
            result.push_back(entry.path());
        }
    }
    return result;
}

std::vector<std::filesystem::path> GetSplittedFileNames(const std::filesystem::path& file_name) {
    LOG_DEBUG << "Checking for splitted files";
    std::vector<std::filesystem::path> part_files = EnumerateByMask(file_name);
    if (part_files.empty()) {
        if (std::filesystem::file_size(file_name) < MaxFileSizeBytes)
        {
            LOG_DEBUG << "File is small enough, skipping splitting";
            return { file_name };
        }

        return SplitVideoFile(file_name);
    }
    return part_files;
}
