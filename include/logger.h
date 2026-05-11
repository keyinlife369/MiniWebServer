#pragma once
//日志
#include<string>
#include<fstream>
#include<mutex>
#include<chrono>
#include<iomanip>
#include<sstream>
#include<iostream>

enum class LogLevel {
	DEBUG,
	INFO,
	WARNING,
	ERROR_
};

class Logger {
public:
	static Logger& getInstance() {
		static Logger instance;
		return instance;
	}
	void setLogFile(const std::string& filename);
	void setLoglevel(LogLevel level);

	template<typename... Args>
	void log(LogLevel level, const std::string& format, Args... args) {
		if (level < min_level_)return;

		std::lock_guard<std::mutex> lock(mutex_);

		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()) % 1000;
		std::stringstream ss;

		ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
			<< "." << std::setfill('0') << std::setw(3) << ms.count()
			<< "[" << levelToString(level) << "]";

		formatHelper(ss, format, args...);
		ss << "\n";
		
		std::string log_msg = ss.str();
		
		if (log_file_.is_open()) {
			log_file_ << log_msg;
			log_file_.flush();
		}
		std::cout << log_msg;
	}

private:
	Logger() = default;
	~Logger() {
		if (log_file_.is_open()) {
			log_file_.close();
		}
	}
	
	std::mutex mutex_;
	std::ofstream log_file_;
	LogLevel min_level_ = LogLevel::DEBUG;

	std::string levelToString(LogLevel level){
		switch (level) {
			case LogLevel::DEBUG:
				return "DEBUG";
			case LogLevel::INFO:
				return "INFO";
			case LogLevel::WARNING:
				return "WARN";
			case LogLevel::ERROR_:
				return "ERROR";
			default:
				return "UNKNOWN";
		}
	}

	void formatHelper(std::stringstream& ss, const std::string& format) {
		ss << format;
	}

	template<typename T>
	void formatHelper(std::stringstream& ss, const std::string& format, T value) {
		size_t pos = format.find("{}");
		if (pos != std::string::npos) {
			ss << format.substr(0, pos);
			ss << value;
			ss << format.substr(pos + 2);
		}
		else {
			ss << format;
		}
	}

	template<typename T, typename... Args>
	void formatHelper(std::stringstream& ss, const std::string& format, T value, Args... args) {
		size_t pos = format.find("{}");
		if (pos != std::string::npos) {
			ss << format.substr(0, pos) << value;
			formatHelper(ss, format.substr(pos + 2), args...);
		}
	}

};


#define LOG_DEBUG(...) Logger::getInstance().log(LogLevel::DEBUG,__VA_ARGS__)
#define LOG_INFO(...) Logger::getInstance().log(LogLevel::INFO,__VA_ARGS__)
#define LOG_WARNING(...) Logger::getInstance().log(LogLevel::WARNING,__VA_ARGS__)
#define LOG_ERROR(...) Logger::getInstance().log(LogLevel::ERROR_,__VA_ARGS__)

