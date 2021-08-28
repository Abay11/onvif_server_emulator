#pragma once

#include "StreamLogger.h"

#include <fstream>

class FileLogger : public StreamLogger
{
public:
	FileLogger(std::unique_ptr<std::fstream> fs, int loggingLevel = LVL_INFO)
		: StreamLogger(*fs.get(), loggingLevel)
		, fs_(std::move(fs))
	{
	}

private:
	std::unique_ptr<std::fstream> fs_;
};