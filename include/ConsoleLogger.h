#pragma once

#include "StreamLogger.h"

class ConsoleLogger : public StreamLogger
{
public:
	ConsoleLogger(int loggingLevel = LVL_INFO) : StreamLogger(std::cout, loggingLevel)
	{
	}
};
