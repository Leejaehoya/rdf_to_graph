#include "log_util.hpp"
#include <fstream>

void appendLog(const std::string& msg) {
    std::ofstream log("run_log.txt", std::ios::app);
    if (!log.is_open()) return;
    log << msg << std::endl;
}


