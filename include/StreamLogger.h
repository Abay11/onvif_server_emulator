#pragma once

#include "../Logger.h"

#include <ostream>

class StreamLogger : public ILogger
{
public:
	StreamLogger(std::ostream& os, int loggingLevel = LVL_INFO) : ILogger(loggingLevel), os_(os)
	{
	}

	void Error(const std::string& msg) const override
	{
		write(msg, LVL_ERR);
	}

	void Warn(const std::string& msg) const override
	{
		write(msg, LVL_WARN);
	}

	void Info(const std::string& msg) const override
	{
		write(msg, LVL_INFO);
	}

	void Debug(const std::string& msg) const override
	{
		write(msg, LVL_DEBUG);
	}

	void Trace(const std::string& msg) const override
	{
		write(msg, LVL_TRACE);
	}

private:
	void write(const std::string& msg, int level) const
	{
		if (level > m_logging_level_)
			return;

		std::stringstream decorated_msg;

		decorated_msg << "[" << utility::datetime::system_utc_time() << "]";

		switch (level)
		{
		case LVL_ERR:
			decorated_msg << "[ERROR] ";
			break;
		case LVL_WARN:
			decorated_msg << "[WARN] ";
			break;
		case LVL_INFO:
			decorated_msg << "[INFO] ";
			break;
		case LVL_DEBUG:
			decorated_msg << "[DEBUG] ";
			break;
		case LVL_TRACE:
			decorated_msg << "[TRACE] ";
			break;
		}

		decorated_msg << msg;

		std::lock_guard<std::mutex> lg(log_writer_mutex);
		os_ << "\n" << decorated_msg.str() << std::endl;
	}

private:
	std::ostream& os_;
	mutable std::mutex log_writer_mutex;
};
