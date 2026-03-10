#include "ffmpeg_video_writer.h"

#include "log.h"
#include "uid_utils.h"

#ifndef _WINDOWS
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

FfmpegVideoWriter::FfmpegVideoWriter(const Settings& settings, const StreamProperties& out_properties)
    : VideoWriter(settings)
    , source_{settings.source}
    , ffmpeg_path_{settings.ffmpeg_path}
    , use_scale_{settings.use_video_scale}
    , output_resolution_{std::to_string(out_properties.width) + "x" + std::to_string(out_properties.height)} {
    file_name_ = (settings.storage_path / (GenerateFileName(VideoWriter::kVideoFilePrefix, &uid_) + VideoWriter::kVideoFileExtension)).string();
}

FfmpegVideoWriter::~FfmpegVideoWriter() {
    Stop();
}

#ifdef _WINDOWS
void FfmpegVideoWriter::Start() {
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string command_line = ffmpeg_path_ + "\\ffmpeg.exe"
        + " -i " + source_
        + (use_scale_ ? (" -s " + output_resolution_) : std::string{})
        + " -acodec aac"
        + " -vcodec " + kVideoCodec + " "
        + file_name_;

    LOG_INFO_EX << "Start ffmpeg, " << LOG_VAR(command_line);

    auto res = CreateProcessA(LPCSTR((ffmpeg_path_ + "\\ffmpeg.exe").c_str()),     // lpApplicationName
                              LPSTR(command_line.c_str()),               // lpCommandLine
                              NULL,                                      // lpProcessAttributes
                              NULL,                                      // lpThreadAttributes
                              TRUE,                                      // bInheritHandles
                              CREATE_NEW_PROCESS_GROUP,                  // dwCreationFlags
                              NULL,                                      // lpEnvironment
                              NULL,                                      // lpCurrentDirectory
                              &si,                                       // lpStartupInfo
                              &pi                                        // lpProcessInformation
    );

    if (!res) {
        DWORD errorCode = GetLastError();
        LOG_ERROR_EX << "Cant start ffmpeg process, " << LOG_VAR(errorCode);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    ffmpeg_pid_ = pi.dwProcessId;
}

void FfmpegVideoWriter::Stop() {
    LOG_INFO_EX << "Shutting down ffmpeg";
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, ffmpeg_pid_)) {
        LOG_ERROR_EX << "Unable to generate console event for ffmpeg";
    }
}

#else

void FfmpegVideoWriter::Start() {
    pid_t child_pid = fork();
    if (child_pid < 0) {
        LOG_ERROR_EX << "Fork failed";
    } else if (child_pid == 0) {
        LOG_INFO_EX << "Start ffmpeg";

        if (use_scale_) {
            execl((ffmpeg_path_ + "/ffmpeg").c_str(),
                "ffmpeg",
                "-i", source_.c_str(),
                "-s", output_resolution_.c_str(),
                "-acodec", "aac",
                "-vcodec", kVideoCodec.c_str(), //"h264"
                file_name_.c_str(),
                (char*)NULL);
        } else {
            execl((ffmpeg_path_ + "/ffmpeg").c_str(),
                "ffmpeg",
                "-i", source_.c_str(),
                "-acodec", "aac",
                "-vcodec", kVideoCodec.c_str(), //"h264"
                file_name_.c_str(),
                (char*)NULL);
        }
    } else {
        ffmpeg_pid_ = child_pid;
        LOG_INFO_EX << "Started ffmpeg, " << LOG_VAR(ffmpeg_pid_);
    }
}

void FfmpegVideoWriter::Stop() {
    LOG_INFO_EX << "Killing process " << ffmpeg_pid_;
    kill(ffmpeg_pid_, SIGTERM);
    int status = 0;
    waitpid(ffmpeg_pid_, &status, 0);
    if (WIFEXITED(status)) {
        LOG_DEBUG << "Exit status = exited";
    } else if (WIFSIGNALED(status)) {
        LOG_DEBUG << "Exit status = signaled";
    } else if (WIFSTOPPED(status)) {
        LOG_DEBUG << "Exit status = stopped";
    } else {
        LOG_DEBUG << "Exit status = unknown";
    }
}

#endif
