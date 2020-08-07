#pragma once

#include <mutex>
#include <iostream>
#include <string>

//should be used only one instance of this class per the whole programm lifecicle
//threadsafe
class Logger
{
public:

	static const int LVL_ERR = 0;
	static const int LVL_WARN = 1;
	static const int LVL_INFO = 2;
	static const int LVL_DEBUG = 3;

	Logger(int loggingLevel = LVL_INFO)
		: m_loggingLevel(loggingLevel)
	{
	}

	void Info(const std::string& msg)
	{
		write(msg, LVL_INFO);
	}

	void Warn(const std::string& msg)
	{
		write(msg, LVL_WARN);
	}

	void Error(const std::string& msg)
	{
		write(msg, LVL_ERR);
	}

private:
	void write(const std::string& msg, int level)
	{
		if (level > m_loggingLevel)
			return;

		std::string decorated_msg;

		switch (level)
		{
		case LVL_ERR:
			decorated_msg += "ERR: ";
			break;
		case LVL_WARN:
			decorated_msg += "WARN: ";
			break;
		case LVL_INFO:
			decorated_msg += "INFO: ";
			break;
		case LVL_DEBUG:
			decorated_msg += "DEBUG: ";
			break;
		}

		decorated_msg += msg;
		
		std::lock_guard<std::mutex> lg(log_writer_mutex);
		std::cout << decorated_msg << std::endl;
	}

private:
	std::mutex log_writer_mutex;

	int m_loggingLevel;
};