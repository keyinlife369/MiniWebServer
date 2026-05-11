#include"logger.h"



void Logger::setLogFile(const std::string& filename)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (log_file_.is_open()) {
		log_file_.close();
	}
	log_file_.open(filename, std::ios::app);
}

void Logger::setLoglevel(LogLevel level)
{
	min_level_ = level;
}
