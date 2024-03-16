#include "telegram_commands_handler.h"

#include "helpers.h"

#include "log.h"

#include "ring_buffer.h"
#include "safe_ptr.h"
#include "translation.h"
#include "uid_utils.h"
#include "video_writer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <regex>
#include <set>

namespace {

}  // namespace

namespace telegram {

CommandsHandler::CommandsHandler() {}

}  // namespace telegram