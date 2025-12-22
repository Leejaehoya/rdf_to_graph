#ifndef LOG_UTIL_HPP
#define LOG_UTIL_HPP

#include <string>

/**
 * 간단한 로그 유틸리티
 * - run_log.txt 파일에 한 줄씩 메시지를 append
 */
void appendLog(const std::string& msg);

#endif // LOG_UTIL_HPP


