#include "DetectorUi.h"
#include "../Server.h"
#include "../include/onvif_services/service_configs.h"
#include "../utility/AnalyticsRules.h"
#include "../utility/ConfigValidation.h"
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
		: rules_(server.Rules()), configs_(server.GetServerConfigs()),
			events_(*static_cast<std::shared_ptr<Tree>>(ServiceConfigs("event", server.ConfigsPath())))
{
	for (const auto* id : {"DigitalInputsAlarm", "MotionAlarm", "CellMotion", "AudioDetection"})
		configs_->detectors->Register(id, events_.get<std::string>(std::string(id) + ".Topic"),
																	events_.get<bool>(std::string(id) + ".GenerateEvents", true));
}
std::string DetectorUi::Read() const
{
	std::vector<Tree> simple;
	for (const auto* id : {"DigitalInputsAlarm", "MotionAlarm", "CellMotion", "AudioDetection"})
	{
		Tree item;
		item.put("id", id);
		item.put("enabled", configs_->detectors->Enabled(id));
		auto settings = events_.get_child(id);
		settings.put("GenerateEvents", configs_->detectors->Enabled(id));
		item.put_child("settings", settings);
		simple.push_back(item);
	}
	std::vector<Tree> families;
	for (const auto& type : Types)
	{
		Tree item;
		item.put("id", type);
		item.put("interval_seconds",
						 type == "tt:MotionRegionDetector" ? events_.get<int>("MotionRegion.EventGenerationTimeout", 2)
						 : type == "tt:LineDetector"			 ? events_.get<int>("LineDetector.EventGenerationTimeout", 2)
																							 : 1);
		item.put("enabled", configs_->detectors->Enabled(type));
		families.push_back(item);
	}
	std::string out = "{\"families\":" + Array(families) +
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
			for (const auto& [___, compatible] : src.get_child("CompatibleAnalyticsConfigurations"))
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
	const auto action = command.get<std::string>("action");
	if (action == "detector")
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
