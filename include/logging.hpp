#pragma once

#include "paper2_scotland2/shared/logger.hpp"

// Logs go to logcat and to the mod's own file under /sdcard/ModData/.../logs.
#define INFO(str, ...) Paper::Logger::fmtLogTag<Paper::LogLevel::INF>(str, MOD_ID __VA_OPT__(, __VA_ARGS__))
#define WARNING(str, ...) Paper::Logger::fmtLogTag<Paper::LogLevel::WRN>(str, MOD_ID __VA_OPT__(, __VA_ARGS__))
#define ERROR(str, ...) Paper::Logger::fmtLogTag<Paper::LogLevel::ERR>(str, MOD_ID __VA_OPT__(, __VA_ARGS__))
#define DEBUG(str, ...) Paper::Logger::fmtLogTag<Paper::LogLevel::DBG>(str, MOD_ID __VA_OPT__(, __VA_ARGS__))
