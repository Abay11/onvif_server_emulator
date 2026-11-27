#include "DetectorUi.h"
#include "../Server.h"
#include "../include/onvif_services/service_configs.h"
#include "../onvif_services/EventSource.h"
#include "../utility/AnalyticsRules.h"
#include "../utility/ChannelTopology.h"
#include "../utility/ConfigValidation.h"
#include "../utility/MediaProfilesManager.h"
#include <algorithm>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <sstream>

namespace osrv
{
namespace
{
using Tree = boost::property_tree::ptree;
const std::vector<std::string> Types{
		"tt:MotionRegionDetector",		"tt:LineDetector",	 "tt:LineCounting", "tt:FieldDetector",
		"tt:LicensePlateRecognition", "tt:FaceRecognition"};
Tree Settings(const Tree& events, const std::string& id, const std::shared_ptr<DetectorControls>& controls, const std::string& source = {})
{
	const auto section = DetectorControls::Section(id);
	Tree settings = events.get_child(section, Tree{});
	const auto path = "ChannelDetectors." + source + "." + section + ".";
	settings.put("Enabled", controls->Enabled(id, source));
	settings.put("AutoGenerateEvents", events.get<bool>(path + "AutoGenerateEvents",
															events.get<bool>(section + ".AutoGenerateEvents", true)));
	settings.put("EventGenerationTimeout", events.get<int>(path + "EventGenerationTimeout",
															events.get<int>(section + ".EventGenerationTimeout", 30)));
	return settings;
}
std::string Json(const Tree& value)
{
	std::ostringstream out;
	boost::property_tree::write_json(out, value, false);
	return out.str();
}
std::string Array(const std::vector<Tree>& values)
{
	std::string out = "[";
	for (const auto& value : values)
	{
		if (out.size() > 1)
			out += ',';
		out += Json(value);
	}
	return out + "]";
}
} // namespace
DetectorUi::DetectorUi(IOnvifServer& server)
		: events_path_(server.ConfigsPath() + "/event.config"), rules_(server.Rules()), configs_(server.GetServerConfigs()),
			events_(*static_cast<std::shared_ptr<Tree>>(ServiceConfigs("event", server.ConfigsPath())))
{
	if (const auto* topology = server.MediaProfilesManager()->Topology())
		for (const auto& [_, channel] : topology->Channels())
			channel_sources_.push_back(channel.get<std::string>("videoSource.token"));

	for (const auto* id : {"DigitalInputsAlarm", "MotionAlarm", "CellMotion", "AudioDetection"})
		configs_->detectors->Register(id, events_.get<std::string>(std::string(id) + ".Topic"),
																	events_.get<bool>(std::string(id) + ".Enabled", true));
}
std::string DetectorUi::Read() const
{
	std::lock_guard lock(mutex_);
	std::vector<Tree> simple;
	for (const auto* id : {"DigitalInputsAlarm", "MotionAlarm", "CellMotion", "AudioDetection"})
	{
		Tree item;
		item.put("id", id);
		item.put("enabled", configs_->detectors->Enabled(id));
		item.put_child("settings", Settings(events_, id, configs_->detectors));
		item.put("manual_available", !configs_->event_source.expired());
		simple.push_back(item);
	}
	std::vector<Tree> families;
	for (const auto& type : Types)
	{
		Tree item;
		item.put("id", type);
		item.put_child("settings", Settings(events_, type, configs_->detectors));
		item.put("interval_seconds", item.get<std::string>("settings.EventGenerationTimeout"));
		item.put("enabled", configs_->detectors->Enabled(type));
		item.put("manual_available", !configs_->event_source.expired());
		families.push_back(item);
	}
	std::vector<Tree> scoped;
	const auto eventSource = configs_->event_source.lock();
	for (const auto& video : rules_->VideoSources())
		for (const auto& family : families)
		{
			const auto id = family.get<std::string>("id");
			Tree item = family;
			item.put("source", video);
			item.put_child("settings", Settings(events_, id, configs_->detectors, video));
			item.put("interval_seconds", item.get<int>("settings.EventGenerationTimeout"));
			item.put("enabled", configs_->detectors->Enabled(id, video));
			item.put("manual_available", eventSource && eventSource->HasDetector(id, video));
			scoped.push_back(std::move(item));
		}
	std::vector<Tree> scopedSimple, channelSources;
	for (const auto& video : channel_sources_)
	{
		Tree source;
		source.put("token", video);
		channelSources.push_back(std::move(source));
		for (const auto& base : simple)
		{
			const auto id = base.get<std::string>("id");
			if (id == "DigitalInputsAlarm" || !eventSource || !eventSource->HasDetector(id, video))
				continue;
			auto item = base;
			item.put("source", video);
			item.put("enabled", configs_->detectors->Enabled(id, video));
			item.put_child("settings", Settings(events_, id, configs_->detectors, video));
			scopedSimple.push_back(std::move(item));
		}
	}
	std::string out = "{\"families\":" + Array(families) + ",\"scoped_families\":" + Array(scoped) +
										",\"channel_sources\":" + Array(channelSources) + ",\"scoped_simple\":" + Array(scopedSimple) +
										",\"rules_enabled\":" + std::string(configs_->detectors->Enabled("rules") ? "true" : "false") +
										",\"max_rules\":4,\"simple\":" + Array(simple) + ",\"configurations\":[";
	bool first = true;
	const auto snapshot = rules_->Snapshot();
	for (const auto& [_, cfg] : snapshot.get_child("Analytics"))
	{
		if (!first)
			out += ',';
		first = false;
		const auto token = cfg.get<std::string>("token");
		Tree header;
		header.put("token", token);
		header.put("name", cfg.get<std::string>("Name", token));
		for (const auto& [__, src] : snapshot.get_child("VideoSource"))
			for (const auto& [key, compatible] : src.get_child("CompatibleAnalyticsConfigurations", Tree{}))
				if (compatible.data() == token && !header.get_child_optional("bounds"))
				{
					header.put_child("bounds", src.get_child("Bounds"));
					header.put("source", src.get<std::string>("SourceToken"));
				}
		auto prefix = Json(header);
		prefix.erase(prefix.find_last_of('}'));
		out += prefix + ",\"rules\":[";
		bool firstRule = true;
		for (const auto& [__, rule] : cfg.get_child("rules"))
		{
			if (!firstRule)
				out += ',';
			firstRule = false;
			Tree item = rule;
			item.put("enabled", configs_->detectors->RuleEnabled(token, rule.get<std::string>("Name")));
			out += Json(item);
		}
		out += "],\"templates\":[";
		bool firstType = true;
		for (const auto& type : Types)
		{
			if (!header.get_child_optional("bounds"))
				break;
			if (!utility::AnalyticsRules::SupportsRule(cfg, type))
				continue;
			if (!firstType)
				out += ',';
			firstType = false;
			out += Json(rules_->DefaultRule(token, type));
		}
		out += "]}";
	}
	return out + "]}";
}
void DetectorUi::Change(const Tree& command)
{
	std::lock_guard lock(mutex_);
	const auto action = command.get<std::string>("action");
	if (action == "settings" || action == "manual")
	{
		const auto id = command.get<std::string>("id");
		if (id != "DigitalInputsAlarm" && id != "MotionAlarm" && id != "CellMotion" && id != "AudioDetection" &&
				std::find(Types.begin(), Types.end(), id) == Types.end())
			throw std::invalid_argument("Unknown detector");
		const auto video = command.get<std::string>("source", "");
		if (!video.empty() && (video.size() > 64 ||
				video.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos))
			throw std::invalid_argument("Unknown video source");
		auto source = configs_->event_source.lock();
		if (!source || !source->HasDetector(id, video))
			throw utility::RuleFault("DetectorUnavailable", true);
		if (action == "manual")
		{
			const auto token = command.get<std::string>("configuration", "");
			const auto name = command.get<std::string>("name", "");
			if (!token.empty() || !name.empty())
			{
				bool found = false;
				const auto snapshot = rules_->Snapshot();
				bool compatible = video.empty();
				for (const auto& [_, config] : snapshot.get_child("VideoSource"))
					if (config.get<std::string>("SourceToken") == video)
						for (const auto& [__, candidate] : config.get_child("CompatibleAnalyticsConfigurations", Tree{}))
							compatible |= candidate.data() == token;
				for (const auto& [_, cfg] : snapshot.get_child("Analytics"))
					if (cfg.get<std::string>("token") == token)
						for (const auto& [__, rule] : cfg.get_child("rules"))
							if (rule.get<std::string>("Name") == name && rule.get<std::string>("Type") == id)
								found = true;
				if (!compatible || !found)
					throw utility::RuleFault("RuleNotExistent");
			}
			try
			{
				source->GenerateOnce(id, token, name, video);
			}
			catch (const std::runtime_error&)
			{
				throw utility::RuleFault("DetectorDisabled", true);
			}
			configs_->journal->Add("info", "system", "detectors",
														 "Manual detector step: " + id + (video.empty() ? "" : " on " + video));
			return;
		}
		const auto& settings = command.get_child("settings");
		for (const auto& [key, _] : settings)
			if (key != "Enabled" && key != "AutoGenerateEvents" && key != "EventGenerationTimeout")
				throw std::invalid_argument("Setting is read-only or unknown: " + key);
		utility::media::ConfigsReaderWriter file(events_path_);
		file.Read();
		auto current = file.ConfigsTree();
		if (current != events_)
			throw utility::RuleFault("ConfigFileChanged", true);
		const auto base = DetectorControls::Section(id);
		const auto section = video.empty() ? base : "ChannelDetectors." + video + "." + base;
		for (const auto& [key, value] : settings)
			current.put_child(section + "." + key, value);
		int interval = 0;
		bool automatic = false, enabled = false;
		file.ConfigsTree() = current;
		file.BeforeSave([&](const std::string& json) {
			utility::ValidateConfig("event.config", json);
			interval = current.get<int>(section + ".EventGenerationTimeout", current.get<int>(base + ".EventGenerationTimeout", 30));
			automatic = current.get<bool>(section + ".AutoGenerateEvents", current.get<bool>(base + ".AutoGenerateEvents", true));
			enabled = current.get<bool>(section + ".Enabled", current.get<bool>(base + ".Enabled", true));
			if (interval < 1)
				throw std::invalid_argument("Invalid event interval");
		});
		file.Save();
		source->ConfigureDetector(id, interval, automatic, enabled, video);
		events_ = std::move(current);
	}
	else if (action == "detector")
	{
		const auto id = command.get<std::string>("id");
		if (id != "rules" && id != "DigitalInputsAlarm" && id != "MotionAlarm" && id != "CellMotion" &&
				id != "AudioDetection" && std::find(Types.begin(), Types.end(), id) == Types.end())
			throw std::invalid_argument("Unknown detector");
		configs_->detectors->Set(id, command.get<bool>("enabled"));
	}
	else
	{
		const auto token = command.get<std::string>("configuration");
		const auto name = command.get<std::string>("name");
		const auto snapshot = rules_->Snapshot();
		const Tree* config = nullptr;
		for (const auto& [_, cfg] : snapshot.get_child("Analytics"))
			if (cfg.get<std::string>("token") == token)
				config = &cfg;
		if (!config)
			throw utility::RuleFault("NoConfig");
		auto existing =
				std::find_if(config->get_child("rules").begin(), config->get_child("rules").end(),
										 [&](const auto& item) { return item.second.template get<std::string>("Name") == name; });
		if (action == "modify" || action == "delete")
		{
			if (existing == config->get_child("rules").end())
				throw utility::RuleFault("RuleNotExistent");
			if (existing->second != command.get_child("previous"))
				throw utility::RuleFault("SettingsChanged", true);
		}
		if (action == "toggle")
		{
			if (existing == config->get_child("rules").end())
				throw utility::RuleFault("RuleNotExistent");
			configs_->detectors->SetRule(token, name, command.get<bool>("enabled"));
		}
		else
		{
			const auto operation = action == "create"		? "CreateRules"
														 : action == "modify" ? "ModifyRules"
														 : action == "delete" ? "DeleteRules"
																									: "";
			if (std::string(operation).empty())
				throw std::invalid_argument("Unknown action");
			Tree request, envelope, document;
			request.put("tan:ConfigurationToken", token);
			if (action == "delete")
				request.put("tan:RuleName", name);
			else
			{
				auto rule = command.get_child("rule");
				if (rule.get<std::string>("Name") != name || name.size() > 256)
					throw utility::RuleFault("InvalidRule");
				const auto type = rule.get<std::string>("Type");
				if (std::find(Types.begin(), Types.end(), type) == Types.end())
					throw utility::RuleFault("InvalidRule");
				request.put_child("tan:Rule", utility::AnalyticsRules::RuleXml(rule));
			}
			envelope.put("<xmlattr>.xmlns:s", "http://www.w3.org/2003/05/soap-envelope");
			envelope.put("<xmlattr>.xmlns:tan", "http://www.onvif.org/ver20/analytics/wsdl");
			envelope.put("<xmlattr>.xmlns:tt", "http://www.onvif.org/ver10/schema");
			envelope.put("<xmlattr>.xmlns:axt", "http://www.onvif.org/ver20/analytics");
			envelope.put_child(std::string("s:Body.tan:") + operation, request);
			document.put_child("s:Envelope", envelope);
			std::ostringstream xml;
			boost::property_tree::write_xml(xml, document);
			try
			{
				rules_->Execute(operation, xml.str());
			}
			catch (const utility::RuleFault& error)
			{
				// Preserve ONVIF's ConfigurationConflict fault, exposing validation
				// details only at the administration JSON boundary.
				try
				{
					std::rethrow_if_nested(error);
				}
				catch (const utility::ConfigValidationError&)
				{
					throw;
				}
				catch (...)
				{
				}
				throw;
			}
		}
	}
	configs_->journal->Add("info", "system", "detectors", "Detector control or rule configuration updated: " + action);
}
} // namespace osrv
