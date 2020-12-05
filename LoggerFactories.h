#pragma once

#include "Logger.h"

#include "ConsoleLogger.h"

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
