#include "MediaProfilesManager.h"

#include <boost/property_tree/json_parser.hpp>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <system_error>
#include <set>
#include <unistd.h>

namespace pt = boost::property_tree;

namespace utility::media
{
namespace
{
std::string Quote(const std::string& value)
{
	pt::ptree wrapper;
	wrapper.put("value", value);
	std::ostringstream output;
	pt::write_json(output, wrapper, false);
	const auto json = output.str();
	return json.substr(json.find(':') + 1, json.rfind('}') - json.find(':') - 1);
}
std::string MediaJson(const pt::ptree& tree, const std::string& key, int depth)
{
	static const std::set<std::string> arrays{"Filters", "rules", "AnalyticsModules", "analyticsModules",
		"compatibleModuleAnalytics", "CompatiblePtzNodes", "CompatibleAnalyticsConfigurations",
		"BitrateList", "SampleRateList", "FrameRatesSupported", "ProfilesSupported", "ResetTime"};
	if (tree.empty())
	{
		if (depth == 1 || arrays.contains(key)) return "[]";
		if (key == "Namespaces") return "{}";
		return Quote(tree.data());
	}
	const bool array = key != "Namespaces" && tree.front().first.empty();
	std::string result = array ? "[" : "{";
	bool first = true;
	for (const auto& [name, child] : tree)
	{
		if (!first) result += ',';
		first = false;
		if (!array) result += Quote(name) + ':';
		result += MediaJson(child, name, depth + 1);
	}
	return result + (array ? "]" : "}");
}
} // namespace
std::string MediaConfigJson(const pt::ptree& tree) { return MediaJson(tree, "", 0); }

ConfigsReaderWriter::ConfigsReaderWriter(const std::string& filePath) : filePath_(filePath)
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
		pt::ptree updated;
		pt::read_json(filePath_, updated);
		*configsTree_ = std::move(updated);
	}
}

void ConfigsReaderWriter::Save() const
{
	if (!configsTree_)
	{
		// it's not initiated
		return;
	}

	std::ostringstream candidate;
	if (std::filesystem::path(filePath_).filename() == "media_profiles.config") candidate << MediaConfigJson(*configsTree_);
	else pt::write_json(candidate, *configsTree_);
	const auto json = candidate.str();
	SaveJson(json);
}

void ConfigsReaderWriter::SaveJson(const std::string& json) const
{
	if (before_save_) before_save_(json);

	auto temporary = filePath_ + ".XXXXXX";
	auto descriptor = mkstemp(temporary.data());
	if (descriptor < 0)
		throw std::system_error(errno, std::generic_category(), "Cannot create configuration temporary file");
	close(descriptor);
	try
	{
		std::ofstream os(temporary);
		os.exceptions(std::ios::failbit | std::ios::badbit);
		os << json;
		os.close();
		std::filesystem::permissions(temporary, std::filesystem::status(filePath_).permissions());
		std::filesystem::rename(temporary, filePath_);
		if (on_save_)
			on_save_();
	}
	catch (...)
	{
		std::error_code ignored;
		std::filesystem::remove(temporary, ignored);
		throw;
	}
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

void MediaProfilesManager::Create(const std::string& profileName,
																	const std::vector<std::pair<std::string, std::string>>& configurations) const
{
	auto& mediaProfilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
	auto generatedToken = newProfileToken(mediaProfilesTree.size());

	pt::ptree newProfileNode;
	newProfileNode.add("token", generatedToken);
	newProfileNode.add("fixed", false);
	newProfileNode.add("Name", profileName);
	ApplyConfigurations(newProfileNode, configurations);

	auto backup = mediaProfilesTree;
	mediaProfilesTree.push_back(make_pair(std::string{}, newProfileNode));
	try
	{
		readerWriter_->Save();
	}
	catch (...)
	{
		mediaProfilesTree = std::move(backup);
		throw;
	}
}

void MediaProfilesManager::Delete(const std::string& profileToken) const
{
	auto& mediaProfilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
	auto begin = mediaProfilesTree.begin();
	auto end = mediaProfilesTree.end();
	auto res_it = std::find_if(begin, end, [&profileToken](const auto& p) {
		return profileToken == p.second.template get<std::string>("token");
	});

	if (res_it == end)
		throw osrv::no_such_profile();

	if (res_it->second.get<std::string>("fixed") == "true")
		throw osrv::deletion_of_fixed_profile();

	auto backup = mediaProfilesTree;
	mediaProfilesTree.erase(res_it);
	try
	{
		readerWriter_->Save();
	}
	catch (...)
	{
		mediaProfilesTree = std::move(backup);
		throw;
	}
}

void MediaProfilesManager::ApplyConfigurations(
		pt::ptree& profile, const std::vector<std::pair<std::string, std::string>>& configurations) const
{
	for (const auto& [type, token] : configurations)
	{
		if (std::ranges::find(osrv::CONFIGURATION_ENUMERATION, type) == osrv::CONFIGURATION_ENUMERATION.end())
			throw osrv::invalid_config_type();
		auto catalog = readerWriter_->ConfigsTree().get_child_optional(type);
		if (!catalog || std::ranges::none_of(*catalog, [&](const auto& item) {
					return item.second.template get<std::string>("token") == token;
				}))
			throw osrv::invalid_token();
		if (type == "Metadata" && !metadata_available_)
			throw osrv::no_config();
		profile.put(type, token);
	}
	if (!profile.get<std::string>("Metadata", "").empty() && profile.get<std::string>("VideoSource", "").empty() &&
			profile.get<std::string>("AudioSource", "").empty())
		throw osrv::configuration_conflict();
}

void MediaProfilesManager::AddConfiguration(const std::string& profileToken, const std::string& configType,
																						const std::string& configToken) const
{
	AddConfigurations(profileToken, {{configType, configToken}});
}

void MediaProfilesManager::AddConfigurations(
		const std::string& profileToken, const std::vector<std::pair<std::string, std::string>>& configurations) const
{
	auto profile = getProfileNode(profileToken);
	auto updated = profile->second;
	ApplyConfigurations(updated, configurations);
	auto backup = profile->second;
	profile->second = std::move(updated);
	try
	{
		readerWriter_->Save();
	}
	catch (...)
	{
		profile->second = std::move(backup);
		throw;
	}
}

void MediaProfilesManager::RemoveConfiguration(std::string_view profileToken, std::string_view configType,
																							 std::string_view configToken) const
{
	RemoveConfigurations(std::string(profileToken), {{std::string(configType), std::string(configToken)}});
}

void MediaProfilesManager::RemoveConfigurations(
		const std::string& profileToken, const std::vector<std::pair<std::string, std::string>>& configurations) const
{
	auto profile = getProfileNode(profileToken);
	auto updated = profile->second;
	for (const auto& [type, token] : configurations)
	{
		if (std::ranges::find(osrv::CONFIGURATION_ENUMERATION, type) == osrv::CONFIGURATION_ENUMERATION.end())
			throw osrv::invalid_config_type();
		if (!token.empty())
		{
			auto found = std::ranges::find_if(updated, [&](const auto& field) {
				return (type == "All" || field.first == type) &&
							 std::ranges::find(osrv::CONFIGURATION_ENUMERATION.begin() + 1, osrv::CONFIGURATION_ENUMERATION.end(),
																 field.first) != osrv::CONFIGURATION_ENUMERATION.end() &&
							 field.second.data() == token;
			});
			if (found == updated.end())
				throw osrv::no_config();
			updated.erase(found);
		}
		else if (type == "All")
			for (const auto& name : osrv::CONFIGURATION_ENUMERATION)
				updated.erase(name);
		else
			updated.erase(type);
	}
	if (!updated.get<std::string>("Metadata", "").empty() && updated.get<std::string>("VideoSource", "").empty() &&
			updated.get<std::string>("AudioSource", "").empty())
		throw osrv::configuration_conflict();
	auto backup = profile->second;
	profile->second = std::move(updated);
	try
	{
		readerWriter_->Save();
	}
	catch (...)
	{
		profile->second = std::move(backup);
		throw;
	}
}

std::string MediaProfilesManager::newProfileToken(size_t n) const
{
	const auto& profiles = readerWriter_->ConfigsTree().get_child("MediaProfiles");
	while (true)
	{
		auto token = std::format("UserProfileToken{}", n++);
		if (std::ranges::none_of(
						profiles, [&](const auto& profile) { return profile.second.template get<std::string>("token") == token; }))
			return token;
	}
}

pt::ptree::iterator MediaProfilesManager::getProfileNode(std::string_view profileToken) const
{
	auto& mediaProfilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
	auto begin = mediaProfilesTree.begin();
	auto end = mediaProfilesTree.end();
	auto profile_it = std::find_if(begin, end, [&profileToken](const auto& p) {
		return profileToken == p.second.template get<std::string>("token");
	});

	if (profile_it == end)
		throw osrv::no_such_profile();

	return profile_it;
}

const boost::property_tree::ptree& MediaProfilesManager::GetProfileByToken(const std::string& token) const
{
	auto& profilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
	auto begin = profilesTree.begin();
	auto end = profilesTree.end();
	auto res_it = std::find_if(begin, end,
														 [&token](const auto& p) { return token == p.second.template get<std::string>("token"); });

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

boost::property_tree::ptree MediaProfilesManager::GetProfileByToken(const std::string& token,
																																		const std::vector<std::string>& configs) const
{

	pt::ptree profileWithFilteredConfigs;

	for (const auto& profileConfig = GetProfileByToken(token); const auto& [name, tree] : profileConfig)
	{
		if (name == "token" || name == "fixed" || name == "Name" || std::ranges::find(configs, "All") != configs.end() ||
				std::ranges::find(configs, name) != configs.end())
		{
			profileWithFilteredConfigs.put(name, tree.data());
		}
	}

	return profileWithFilteredConfigs;
}

const boost::property_tree::ptree& MediaProfilesManager::GetConfigByToken(std::string_view token,
																																					std::string_view configType) const
{
	auto catalog = readerWriter_->ConfigsTree().get_child_optional(configType.data());
	if (!catalog) throw osrv::no_config{};
	auto& configsTree = *catalog;
	const auto res_it = std::ranges::find_if(
			configsTree, [&token](const auto& p) { return token == p.second.template get<std::string>("token"); });

	if (res_it == configsTree.end())
		throw osrv::no_config{};

	return res_it->second;
}

boost::property_tree::ptree& MediaProfilesManager::GetProfileByName(const std::string& name)
{
	auto& profilesTree = readerWriter_->ConfigsTree().get_child("MediaProfiles");
	auto begin = profilesTree.begin();
	auto end = profilesTree.end();
	auto res_it = std::find_if(begin, end, [&name](const auto& p) { return name == p.second.template get<std::string>("Name"); });

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

size_t MediaProfilesManager::GetUseCount(std::string_view token, std::string_view configType) const
{
	size_t res = 0;
	for (const auto& profilesConfigs = readerWriter_->ConfigsTree().get_child("MediaProfiles", {});
			 const auto& [key, node] : profilesConfigs)
	{
		res += (int)(node.get<std::string>(configType.data(), "") == token);
	}

	return res;
}

ProfileConfigsHelper::ProfileConfigsHelper(const pt::ptree& profileTree) : profileTree_(profileTree)
{
}

std::string ProfileConfigsHelper::ProfileToken() const
{
	return profileTree_.get<std::string>("token");
}

} // namespace utility::media
