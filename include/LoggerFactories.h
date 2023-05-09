#pragma once

#include "../Logger.h"

#include "ConsoleLogger.h"
#include "FileLogger.h"

#include <memory>

class ILoggerFactory
{
public:
	virtual ILogger* GetLogger(int log_level) = 0;
};

class ConsoleLoggerFactory : public ILoggerFactory
{
public:
	ILogger* GetLogger(int log_level) override
	{
		return new ConsoleLogger(log_level);
	}
};

class FileLoggerFactory : public ILoggerFactory
{
public:
	FileLoggerFactory(const std::string& fileName) : fileName_(fileName)
	{
	}

	ILogger* GetLogger(int log_level) override
	{
		auto fs = std::make_unique<std::fstream>(fileName_, std::ios_base::out | std::ios_base::app);
		return new FileLogger(std::move(fs), log_level);
	}

private:
	const std::string fileName_;
};
