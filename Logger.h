#pragma once

#include "./utility/DateTime.hpp"


#include <boost/property_tree/ptree.hpp>

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

	std::string GetLogLevel()
	{
		switch (m_logging_level_)
		{
			case LVL_ERR: return "ERROR";
			case LVL_WARN: return "WARN";
			case LVL_INFO: return "INFO";
			case LVL_TRACE: return "TRACE";
			default: LVL_DEBUG: return "DEBUG";
		}
	}

protected:
	int m_logging_level_;
};

using pt_ptr = std::shared_ptr<boost::property_tree::ptree>;
class LoggerConfigs
{
public:
	LoggerConfigs(const pt_ptr configs) : configs_(configs) {}

	int LogLevel()
	{
		auto log_lvl = configs_->get<std::string>("loggingLevel", "");
		return to_lvl(log_lvl);
	}

	std::string LogOutput()
	{
		return configs_->get<std::string>("loggingOutput", "");
	}

private:
	int to_lvl(const std::string& lvl_str)
	{
		if (lvl_str == "ERROR")
			return ILogger::LVL_ERR;
		
		if (lvl_str == "WARN")
			return ILogger::LVL_WARN;
		
		if (lvl_str == "INFO")
			return ILogger::LVL_INFO;

		if (lvl_str == "DEBUG")
			return ILogger::LVL_DEBUG;

		if (lvl_str == "TRACE")
			return ILogger::LVL_TRACE;
		
		// as default
		return ILogger::LVL_DEBUG;
	}

private:
	const pt_ptr configs_;
};

#define TRACE_LOG(ILogger_instance) ILogger_instance.Trace(__FUNCTION__)
