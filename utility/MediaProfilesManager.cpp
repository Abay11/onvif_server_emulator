#include "MediaProfilesManager.h"

#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;

namespace utility::media
{
	ConfigsReaderWriter::ConfigsReaderWriter(const std::string& filePath)
		: filePath_(filePath)
	{
	}

	void ConfigsReaderWriter::Read()
	{
		if (!configsTreeBackup_)
		{
			// initial read
			configsTreeBackup_ = std::make_unique<pt::ptree>();
			pt::read_json(filePath_, *configsTreeBackup_);
			configsTree_ = std::make_unique<pt::ptree>(*configsTreeBackup_);
		}
	}

	void ConfigsReaderWriter::Save()
	{
		if (!configsTree_)
		{
			// it's not initiated
			return;
		}

		std::ofstream os(filePath_);
		pt::write_json(os, *configsTree_);
	}

	void ConfigsReaderWriter::Reset()
	{
		if (!configsTreeBackup_)
		{
			// it's not initiated
			return;
		}

		*configsTree_ = *configsTreeBackup_;
		Save();
	}

	MediaProfilesManager::MediaProfilesManager(const std::string& filePath)
	{
	}
}
