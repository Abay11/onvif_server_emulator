#pragma once

#include "./utility/DateTime.hpp"

#include <mutex>
#include <iostream>
#include <string>

class ILogger
{
public:
	static const int LVL_ERR = 0;
	static const int LVL_WARN = 1;
	static const int LVL_INFO = 2;
	static const int LVL_DEBUG = 3;
	static const int LVL_TRACE = 4;

	virtual void Error(const std::string& msg) const = 0;
	virtual void Warn(const std::string& msg) const = 0;
	virtual void Info(const std::string& msg) const = 0;
	virtual void Debug(const std::string& msg) const = 0;
	virtual void Trace(const std::string& msg) const = 0;

	ILogger(int log_lvl)
		: m_logging_level_(log_lvl){}


	void SetLogLevel(int l)
	{
		m_logging_level_ = l;
	}

	static int to_lvl(const std::string& lvl_str)
	{
		if (lvl_str == "ERROR")
			return LVL_ERR;
		
		if (lvl_str == "WARN")
			return LVL_WARN;
		
		if (lvl_str == "INFO")
			return LVL_INFO;

		if (lvl_str == "DEBUG")
			return LVL_DEBUG;

		if (lvl_str == "TRACE")
			return LVL_TRACE;
		
		// as default
		return LVL_DEBUG;
	}

protected:
	int m_logging_level_;
};
