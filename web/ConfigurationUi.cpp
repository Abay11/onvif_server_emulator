#include "ConfigurationUi.h"
#include "../Server.h"
#include "../utility/AnalyticsRules.h"
#include "../utility/ConfigValidation.h"
#include "../utility/MediaProfilesManager.h"
#include "../utility/MetadataConfiguration.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fstream>
#include <set>
#include <sstream>

namespace osrv
{
namespace
{
using Tree = boost::property_tree::ptree;
const Tree Empty;
const std::vector<std::string> Files{
		"common.config", "device.config",			"device_io.config",				 "imaging.config",				"media.config",
		"media2.config", "ptz.config",				"recording_search.config", "replay_control.config", "discovery.config",
		"event.config",	 "io_sources.config", "media_profiles.config",	 "analytics.config"};
constexpr const char* Secret = "__UNCHANGED_SECRET__";
std::string Json(const Tree& tree)
{
	std::ostringstream output;
	boost::property_tree::write_json(output, tree, false);
	return output.str();
}
Tree Parse(const std::string& bytes)
{
	Tree result;
	std::istringstream input(bytes);
	boost::property_tree::read_json(input, result);
	return result;
}
std::string ReadFile(const std::string& path)
{
	std::ifstream input(path);
	if (!input)
		throw std::runtime_error("Configuration file unavailable");
	return std::string(std::istreambuf_iterator<char>(input), {});
}
std::string Revision()
{
	return boost::uuids::to_string(boost::uuids::random_generator()());
}
// Replace only password string tokens, preserving all other JSON types and bytes.
std::string PasswordJson(const std::string& bytes, const Tree& accounts, bool redact)
{
	std::vector<std::string> passwords;
	for (const auto& [_, account] : accounts.get_child("users"))
	{
		Tree wrapper;
		wrapper.put("value", redact ? Secret : account.get<std::string>("password"));
		const auto json = Json(wrapper);
		passwords.push_back(json.substr(json.find(':') + 1, json.rfind('}') - json.find(':') - 1));
	}
	auto endString = [&](std::size_t start) {
		for (auto i = start + 1; i < bytes.size(); ++i)
		{
			if (bytes[i] == '\\')
				++i;
			else if (bytes[i] == '"')
				return i + 1;
		}
		throw std::invalid_argument("Invalid JSON string");
	};
	std::string result;
	std::size_t copied = 0, account = 0;
	for (std::size_t i = 0; i < bytes.size();)
	{
		if (bytes[i] != '"')
		{
			++i;
			continue;
		}
		const auto end = endString(i);
		const auto colon = bytes.find_first_not_of(" \r\n\t", end);
		if (colon != std::string::npos && bytes[colon] == ':' &&
				Parse("{\"key\":" + bytes.substr(i, end - i) + "}").get<std::string>("key") == "password")
		{
			const auto start = bytes.find_first_not_of(" \r\n\t", colon + 1);
			if (start == std::string::npos || bytes[start] != '"' || account == passwords.size())
				throw std::invalid_argument("Invalid account password");
			const auto finish = endString(start);
			result += bytes.substr(copied, start - copied) + passwords[account++];
			copied = finish;
			i = finish;
		}
		else
			i = end;
	}
	return result + bytes.substr(copied);
}
void RestorePasswords(Tree& candidate, const Tree& previous)
{
	std::set<std::string> logins;
	for (auto& [_, account] : candidate.get_child("users"))
	{
		const auto login = account.get<std::string>("login");
		if (!logins.insert(login).second)
			throw std::invalid_argument("Duplicate login");
		if (account.get<std::string>("password") != Secret)
			continue;
		const Tree* old = nullptr;
		for (const auto& [__, stored] : previous.get_child("users"))
			if (stored.get<std::string>("login") == login)
				old = &stored;
		if (!old)
			throw std::invalid_argument("A new account needs a password");
		account.put("password", old->get<std::string>("password"));
	}
	const auto low = candidate.get<unsigned>("rtp.port_min", 50000);
	const auto high = candidate.get<unsigned>("rtp.port_max", 50199);
	if (low == 0 || low % 2 != 0 || high <= low || high % 2 != 1)
		throw std::invalid_argument("Invalid RTP port range");
}
const Tree& Find(const Tree& media, const std::string& type, const std::string& token)
{
	if (auto catalog = media.get_child_optional(type))
		for (const auto& [_, item] : *catalog)
			if (item.get<std::string>("token", "") == token)
				return item;
	throw std::invalid_argument("Unknown " + type + " configuration: " + token);
}
bool Contains(const Tree& list, const std::string& token)
{
	return std::any_of(list.begin(), list.end(), [&](const auto& item) { return item.second.data() == token; });
}
void ValidateMedia(const Tree& media, const Tree& previous, bool metadataEnabled, const std::string& path,
									 const std::string& address)
{
	for (const auto& [type, catalog] : media)
	{
		std::set<std::string> tokens;
		for (const auto& [_, item] : catalog)
			if (auto token = item.get_optional<std::string>("token"))
				if (!tokens.insert(*token).second)
					throw std::invalid_argument("Duplicate " + type + " token");
	}
	for (const auto& [_, old] : previous.get_child("MediaProfiles"))
		if (old.get<bool>("fixed", false))
		{
			const auto& profile = Find(media, "MediaProfiles", old.get<std::string>("token"));
			if (!profile.get<bool>("fixed", false))
				throw std::invalid_argument("A fixed profile cannot become deletable");
		}
	for (const auto& [_, profile] : media.get_child("MediaProfiles"))
	{
		for (const auto& type : CONFIGURATION_ENUMERATION)
			if (type != "All")
				if (const auto token = profile.get<std::string>(type, ""); !token.empty())
					Find(media, type, token);
		const auto metadata = profile.get<std::string>("Metadata", "");
		const auto source = profile.get<std::string>("VideoSource", "");
		if (!metadata.empty() && !metadataEnabled)
			throw std::invalid_argument("Metadata transport is disabled");
		if (!metadata.empty() && source.empty() && profile.get<std::string>("AudioSource", "").empty())
			throw std::invalid_argument("Metadata needs a video or audio source");
		if (!source.empty())
		{
			const auto& video = Find(media, "VideoSource", source);
			const auto analytics = profile.get<std::string>("Analytics", "");
			if (!analytics.empty() && !Contains(video.get_child("CompatibleAnalyticsConfigurations", Tree{}), analytics))
				throw std::invalid_argument("Analytics configuration is incompatible with the video source");
			const auto ptz = profile.get<std::string>("PTZ", "");
			if (!ptz.empty() && !Contains(video.get_child("CompatiblePtzNodes", Tree{}),
																		Find(media, "PTZ", ptz).get<std::string>("NodeToken")))
				throw std::invalid_argument("PTZ configuration is incompatible with the video source");
		}
	}
	for (const auto& [_, item] : media.get_child("Metadata", Empty))
		metadata::Compile(item);
	// Reuse native analytics validation for geometry, bindings and rule limits.
	utility::AnalyticsRules check(media, {}, std::chrono::system_clock::now, utility::lpr::LoadImages(path, address),
																utility::face::LoadImages(path, address));
}
bool OnlyLiveMedia(const Tree& next, const Tree& previous)
{
	auto a = next, b = previous;
	for (const auto* key : {"MediaProfiles", "Metadata"})
	{
		a.erase(key);
		b.erase(key);
	}
	for (auto* tree : {&a, &b})
		for (auto& [_, catalog] : *tree)
			for (auto& [__, config] : catalog)
				config.erase("UseCount");
	return a == b;
}
} // namespace
ConfigurationUi::ConfigurationUi(IOnvifServer& server) : server_(server)
{
}
std::string ConfigurationUi::Read()
{
	std::lock_guard lock(mutex_);
	std::string result = "{\"files\":[";
	bool first = true;
	for (const auto& file : Files)
	{
		const auto bytes = ReadFile(server_.ConfigsPath() + "/" + file);
		auto& snapshot = snapshots_[file];
		if (snapshot.bytes != bytes || snapshot.revision.empty())
			snapshot = {bytes, Revision()};
		Tree entry;
		entry.put("name", file);
		entry.put("revision", snapshot.revision);
		if (file == "common.config")
		{
			entry.put("document", PasswordJson(bytes, Parse(bytes), true));
		}
		else
			entry.put("document", bytes);
		if (!first)
			result += ',';
		first = false;
		result += Json(entry);
	}
	return result + "],\"metadata_enabled\":" + (server_.GetServerConfigs()->rtsp_metadata_enabled_ ? "true" : "false") +
				 ",\"restart_required\":" + (restart_required_ ? "true" : "false") + "}";
}
std::string ConfigurationUi::Change(const Tree& command)
{
	std::lock_guard lock(mutex_);
	const auto file = command.get<std::string>("file");
	if (std::find(Files.begin(), Files.end(), file) == Files.end())
		throw std::invalid_argument("Unknown configuration file");
	auto found = snapshots_.find(file);
	if (found == snapshots_.end() || command.get<std::string>("revision") != found->second.revision ||
			ReadFile(server_.ConfigsPath() + "/" + file) != found->second.bytes)
		throw utility::RuleFault("SettingsChanged", true);
	auto bytes = command.get<std::string>("document");
	// Validate exact JSON bytes first, including duplicate keys and JSON types.
	utility::ValidateConfig(file, bytes);
	auto candidate = Parse(bytes);
	if (file == "common.config")
	{
		const auto previous = Parse(found->second.bytes);
		RestorePasswords(candidate, previous);
		bytes = PasswordJson(bytes, candidate, false);
		utility::ValidateConfig(file, bytes);
	}
	const auto media = server_.MediaProfilesManager();
	const bool isMedia = file == "media_profiles.config";
	bool restart = !isMedia;
	if (isMedia)
	{
		if (Parse(found->second.bytes) != media->ReaderWriter()->ConfigsTree())
			throw utility::RuleFault("SettingsChanged", true);
		ValidateMedia(candidate, media->ReaderWriter()->ConfigsTree(), server_.GetServerConfigs()->rtsp_metadata_enabled_,
									server_.ConfigsPath(), server_.GetServerConfigs()->HttpAddress());
		restart = !OnlyLiveMedia(candidate, media->ReaderWriter()->ConfigsTree());
		for (auto& [type, catalog] : candidate)
			for (auto& [_, config] : catalog)
				if (config.get_child_optional("UseCount"))
				{
					unsigned count = 0;
					for (const auto& [__, profile] : candidate.get_child("MediaProfiles"))
						if (profile.get<std::string>(type == "VideoEncoderConfigurations" ? "VideoEncoder" : type, "") ==
								config.get<std::string>("token"))
							++count;
					config.put("UseCount", count);
				}
		bytes = utility::media::MediaConfigJson(candidate);
		utility::ValidateConfig(file, bytes);
	}
	utility::media::ConfigsReaderWriter writer(server_.ConfigsPath() + "/" + file);
	writer.Read();
	writer.BeforeSave([&](const std::string& json) { utility::ValidateConfig(file, json); });
	// File saves preserve empty arrays/objects and numeric types exactly as entered.
	writer.SaveJson(bytes);
	if (isMedia)
	{
		media->ReaderWriter()->ConfigsTree() = std::move(candidate);
		try
		{
			server_.Metadata()->Refresh();
		}
		catch (const std::exception&)
		{
			restart = true;
		}
	}
	found->second = {bytes, Revision()};
	restart_required_ = restart_required_ || restart;
	server_.GetServerConfigs()->journal->Add("info", "system", "configuration", "Configuration saved: " + file);
	return std::string("{\"status\":\"saved\",\"restart_required\":") + (restart ? "true" : "false") + "}";
}
} // namespace osrv
