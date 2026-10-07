#include "platform/Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

#ifdef TARGET_PC
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <filesystem>
#endif

#if PLATFORM_WII
extern "C" void wiiPlatformLogWrite(const char* line);
#endif

namespace
{

#ifdef TARGET_PC
// func to get Date for Log file e.g (debug-yy-mm-dd.log)
std::string getDatedLogFilename()
{
    auto now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf{};

#if defined(_WIN32)
    localtime_s(&buf, &in_time_t);
#else
    localtime_r(&in_time_t, &buf);
#endif

    std::ostringstream ss;
    // Format: debug-YY-MM-DD.log
    ss << "debug-" 
       << std::setfill('0') << std::setw(2) << (buf.tm_year % 100) << "-"
       << std::setfill('0') << std::setw(2) << (buf.tm_mon + 1) << "-"
       << std::setfill('0') << std::setw(2) << buf.tm_mday
       << ".log";

    return ss.str();
}

#endif
#ifdef TARGET_PC
std::shared_ptr<spdlog::logger> g_pcLogger = nullptr;
#else
FILE* g_logFile = nullptr;
alignas(64) char g_fileBuffer[8192];
alignas(64) char g_earlyLog[16384];
std::size_t g_earlyLogSize = 0;
bool g_earlyLogTruncated = false;
bool g_syncWrites = MC_LOG_SYNC_WRITES != 0;
char g_logPath[512] = {};
int g_sinceCommit = 0;
#endif

const char* levelName(McLog::Level level)
{
    switch (level)
    {
        case McLog::Level::Error:   return "E";
        case McLog::Level::Warning: return "W";
        case McLog::Level::Info:    return "I";
        case McLog::Level::Debug:   return "D";
        case McLog::Level::Trace:   return "T";
    }
    return "?";
}

#ifndef TARGET_PC
void appendEarly(const char* line)
{
#if defined(PS2_REMOTE_DEBUG) && PLATFORM_PS2
    (void)line;
    return;
#else
    if (!line || g_logFile)
        return;

    const std::size_t len = std::strlen(line);
    const std::size_t room = sizeof(g_earlyLog) - g_earlyLogSize;
    if (len <= room)
    {
        std::memcpy(g_earlyLog + g_earlyLogSize, line, len);
        g_earlyLogSize += len;
    }
    else
    {
        g_earlyLogTruncated = true;
    }
#endif
}

void commitFile()
{
    if (!g_logFile || !g_logPath[0])
        return;

    std::fflush(g_logFile);
    std::fclose(g_logFile);
    g_sinceCommit = 0;

    g_logFile = std::fopen(g_logPath, "a");
    if (g_logFile)
        std::setvbuf(g_logFile, g_fileBuffer, _IOFBF, sizeof(g_fileBuffer));
}
#endif
}

bool McLog::openSessionFile(const char* directory)
{
#if MC_LOG_LEVEL > 0
#ifdef TARGET_PC
    if (g_pcLogger)
        return true;

    try
    {
        std::string dir = (directory && *directory) ? directory : "log";
        
        std::filesystem::create_directories(dir);

        std::string filename = getDatedLogFilename();
        std::string logFilePath = dir + "/" + filename;

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true);

        std::vector<spdlog::sink_ptr> sinks { console_sink, file_sink };
        g_pcLogger = std::make_shared<spdlog::logger>("OptiCraft", sinks.begin(), sinks.end());
        
        g_pcLogger->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
        g_pcLogger->set_level(spdlog::level::trace);
        g_pcLogger->flush_on(spdlog::level::warn);

        spdlog::register_logger(g_pcLogger);
        
        g_pcLogger->info("Log init in: {}", logFilePath);
        return true;
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        std::printf("Cant init spdlog: %s\n", ex.what());
        return false;
    }
#else
    if (g_logFile)
        return true;
    if (!directory || !*directory)
        return false;

    char path[512];
    const std::size_t len = std::strlen(directory);
    const bool hasSeparator = len > 0 && (directory[len - 1] == '/' || directory[len - 1] == '\\' || directory[len - 1] == ':');
    std::snprintf(path, sizeof(path), hasSeparator ? "%sdebug.log" : "%s/debug.log", directory);

    FILE* file = std::fopen(path, "w");
    if (!file)
        return false;

    g_logFile = file;
    std::setvbuf(g_logFile, g_fileBuffer, _IOFBF, sizeof(g_fileBuffer));
    std::snprintf(g_logPath, sizeof(g_logPath), "%s", path);
    g_sinceCommit = 0;

    if (g_earlyLogSize > 0)
        std::fwrite(g_earlyLog, 1, g_earlyLogSize, g_logFile);

    return true;
#endif
#else
    (void)directory;
    return false;
#endif
}

void McLog::flush()
{
#if MC_LOG_LEVEL > 0
#ifdef TARGET_PC
    if (g_pcLogger)
        g_pcLogger->flush();
#else
    if (g_logFile)
        commitFile();
#endif
#endif
}

void McLog::setSyncWrites(bool enabled)
{
#ifndef TARGET_PC
#if MC_LOG_LEVEL > 0
    g_syncWrites = enabled;
    if (enabled && g_logFile)
        std::fflush(g_logFile);
#endif
#else
    (void)enabled;
#endif
}

bool McLog::syncWrites()
{
#ifndef TARGET_PC
#if MC_LOG_LEVEL > 0
    return g_syncWrites;
#endif
#endif
    return false;
}

void McLog::resetPlatformLog()
{
#if MC_LOG_LEVEL > 0
#ifdef TARGET_PC
    if (g_pcLogger)
    {
        g_pcLogger->flush();
        spdlog::drop("OptiCraft");
        g_pcLogger = nullptr;
    }
#else
    if (g_logFile)
    {
        std::fflush(g_logFile);
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
    g_logPath[0] = '\0';
#endif
#endif
}

void McLog::write(Level level, const char* category, const char* fmt, ...)
{
#if MC_LOG_LEVEL > 0
    char message[768];
    va_list ap;
    va_start(ap, fmt);
    ::vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    const char* safeCategory = category ? category : "game";

#ifdef TARGET_PC
    if (!g_pcLogger)
    {
        openSessionFile("log");
    }

    if (g_pcLogger)
    {
        std::string formattedMsg = fmt::format("[{}] {}", safeCategory, message);
        switch (level)
        {
            case Level::Error:   g_pcLogger->error(formattedMsg); break;
            case Level::Warning: g_pcLogger->warn(formattedMsg); break;
            case Level::Info:    g_pcLogger->info(formattedMsg); break;
            case Level::Debug:   g_pcLogger->debug(formattedMsg); break;
            case Level::Trace:   g_pcLogger->trace(formattedMsg); break;
        }
    }
#else
    char line[896];
    ::snprintf(line, sizeof(line), "[MC][%s][%s] %s\n", levelName(level), safeCategory, message);

    if (g_logFile)
        writeFile(line, level, safeCategory);
    else
        appendEarly(line);
#endif
#endif
}