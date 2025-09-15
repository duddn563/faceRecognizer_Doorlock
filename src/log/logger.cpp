#include "logger.hpp"
#include <fstream>
#include <iostream>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdarg>   // va_list
#include <cstdio>    // vsnprintf
#include <cstring>
#include <errno.h>

void Logger::write(const std::string& message) {
    std::string dir = LOG_DIR;
    std::string filePath = LOG_FILE;

    // 디렉토리가 없으면 생성
    if (access(dir.c_str(), F_OK) == -1) {
        if (mkdir(dir.c_str(), 0777) == -1) {
            std::cerr << "로그 디렉토리 생성 실패: " << strerror(errno) << std::endl;
            return;
        }
    }

    // 현재 시간 가져오기
    time_t now = time(0);
    tm* ltm = localtime(&now);

    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", ltm);

    // 로그 파일에 추가 쓰기
    std::ofstream logFile(filePath, std::ios::app);
    if (logFile.is_open()) {
        logFile << "[" << timeStr << "] " << message << std::endl;
        logFile.close();
    } else {
        std::cerr << "로그 파일 열기 실패!" << std::endl;
    }
}

void Logger::writef(const char* format, ...)
{
		char buffer[1024];

		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);

		write(std::string(buffer));
}

