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

	boost::property_tree::ptree& MediaProfilesManager::GetProfileByToken(const std::string& token)
	{
		auto& profilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
		auto begin = profilesTree.begin();
		auto end = profilesTree.end();
		auto res_it = std::find_if(begin, end,
			[token](const auto& p) { return token == p.second.get<std::string>("token"); });

		if (res_it == end)
		{
			throw osrv::no_such_profile();
		}

		return res_it->second;
	}
		
	boost::property_tree::ptree& MediaProfilesManager::GetProfileByName(const std::string& name)
	{
		auto& profilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
		auto begin = profilesTree.begin();
		auto end = profilesTree.end();
		auto res_it = std::find_if(begin, end,
			[name](const auto& p) { return name == p.second.get<std::string>("Name"); });

		if (res_it == end)
		{
			throw osrv::no_such_profile();
		}

		return res_it->second;
	}
}
