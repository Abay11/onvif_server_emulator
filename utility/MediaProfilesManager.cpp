#include "MediaProfilesManager.h"

#include <boost/property_tree/json_parser.hpp>

#include <iostream>

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
		else
		{
			configsTree_.reset();
			configsTree_ = std::make_unique<pt::ptree>();
			pt::read_json(filePath_, *configsTree_);
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
		readerWriter_ = std::make_unique<ConfigsReaderWriter>(filePath);
		readerWriter_->Read();
	}
	
	void MediaProfilesManager::Create(const std::string& profileName) const
	{
		auto& mediaProfilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
		auto generatedToken = newProfileToken(mediaProfilesTree.size());

		pt::ptree newProfileNode;
		newProfileNode.add("token", generatedToken);
		newProfileNode.add("fixed", false);
		newProfileNode.add("Name", profileName);

		mediaProfilesTree.push_back(make_pair(std::string{}, newProfileNode));

		readerWriter_->Save();
	}

	void MediaProfilesManager::Delete(const std::string& profileToken) const
	{
		auto& mediaProfilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
		auto begin = mediaProfilesTree.begin();
		auto end = mediaProfilesTree.end();
		auto res_it = std::find_if(begin, end,
			[profileToken](const auto& p) { return profileToken == p.second.get<std::string>("token"); });

		if (res_it == end)
			throw osrv::no_such_profile();

		if (res_it->second.get<std::string>("fixed") == "true")
			throw osrv::deletion_of_fixed_profile();

		mediaProfilesTree.erase(res_it);

		readerWriter_->Save();
	}

	void MediaProfilesManager::AddConfiguration(const std::string& profileToken, const std::string& configType, const std::string& configToken) const
	{
		auto& mediaProfilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");

		auto begin = mediaProfilesTree.begin();
		auto end = mediaProfilesTree.end();
		auto res_it = std::find_if(begin, end,
			[profileToken](const auto& p) { return profileToken == p.second.get<std::string>("token"); });

		if (res_it == end)
			throw osrv::no_such_profile();

		if (osrv::CONFIGURATION_ENUMERATION.end() ==
			std::find(osrv::CONFIGURATION_ENUMERATION.begin(), osrv::CONFIGURATION_ENUMERATION.end(), configType))
			throw osrv::invalid_config_type();

		const auto& config_tree = readerWriter_->ConfigsTree().get_child(configType);
		auto config_tree_res_it = std::find_if(config_tree.begin(), config_tree.end(),
			[&configToken](const auto& it) { return configToken == it.second.get<std::string>("token"); });

		if (config_tree_res_it == config_tree.end())
			throw osrv::invalid_token();

		res_it->second.add(configType, configToken);

		readerWriter_->Save();
	}

	void MediaProfilesManager::RemoveConfiguration(std::string_view profileToken, std::string_view configType, std::string_view configToken) const
	{
		auto& mediaProfilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");

		auto begin = mediaProfilesTree.begin();
		auto end = mediaProfilesTree.end();
		auto profile_it = std::find_if(begin, end,
			[&profileToken](const auto& p) { return profileToken == p.second.get<std::string>("token"); });

		if (profile_it == end)
			throw osrv::no_such_profile();

		// if config token is provided, remove by config token
		if (!configToken.empty())
		{
			auto config_it = std::find_if(profile_it->second.begin(), profile_it->second.end(),
				[&configToken](const auto& item)
				{
					auto key = item.first;

					static const std::vector INSTANCES_ALLOWED_TO_REMOVE (osrv::CONFIGURATION_ENUMERATION.begin() + 1,
						osrv::CONFIGURATION_ENUMERATION.end());

					bool isConfig = std::any_of( INSTANCES_ALLOWED_TO_REMOVE.begin(), INSTANCES_ALLOWED_TO_REMOVE.end(),
						[&key](auto configType) { return key == configType; });
					
					return isConfig && configToken == item.second.get_value<std::string>();
				});

			if (config_it != profile_it->second.end())
			{
				profile_it->second.erase(config_it);
				readerWriter_->Save();
				return;
			}
		}

		if (osrv::CONFIGURATION_ENUMERATION.end() ==
			std::find(osrv::CONFIGURATION_ENUMERATION.begin(), osrv::CONFIGURATION_ENUMERATION.end(), configType))
			throw osrv::invalid_config_type();

		profile_it->second.erase(configType.data());

		readerWriter_->Save();
	}
		
	std::string MediaProfilesManager::newProfileToken(size_t n) const
	{
		auto token = "UserProfileToken" + std::to_string(n);
		return token;
	}

	const boost::property_tree::ptree& MediaProfilesManager::GetProfileByToken(const std::string& token) const
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

	boost::property_tree::ptree& MediaProfilesManager::GetProfileByToken(const std::string& token)
	{
		return const_cast<boost::property_tree::ptree&>(
			const_cast<MediaProfilesManager const*>(this)->GetProfileByToken(token));
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

	const boost::property_tree::ptree& MediaProfilesManager::Back() const
	{
		return readerWriter_->ConfigsTree().get_child("MediaProfiles").back().second;
	}
		
	ProfileConfigsHelper::ProfileConfigsHelper(const pt::ptree& profileTree)
		: profileTree_(profileTree)
	{
	}

	std::string ProfileConfigsHelper::ProfileToken() const
	{
		return profileTree_.get<std::string>("token");
	}
}
