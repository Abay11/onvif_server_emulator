#include "MotionRegionDetector.h"
#include "DateTime.hpp"
#include "XmlParser.h"
#include <boost/algorithm/string/trim.hpp>
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <cmath>
#include <set>

namespace utility
{
namespace
{
using Tree = boost::property_tree::ptree;
namespace bg = boost::geometry;
using Point = bg::model::d2::point_xy<double>;
using Polygon = bg::model::polygon<Point>;
const std::string Schema = "http://www.onvif.org/ver10/schema";
const std::string Analytics = "http://www.onvif.org/ver20/analytics/wsdl";
const std::string Rules = "http://www.onvif.org/ver20/analytics";

// Resolve both element names and QName values using their in-scope namespaces.
Tree canonical(const Tree& input, std::map<std::string, std::string> ns = {})
{
	if (auto attrs = input.get_child_optional("<xmlattr>"))
		for (const auto& [key, value] : *attrs)
			if (key == "xmlns")
				ns[""] = value.data();
			else if (key.starts_with("xmlns:"))
				ns[key.substr(6)] = value.data();
	auto qname = [&](std::string value) {
		boost::algorithm::trim(value);
		auto colon = value.find(':');
		auto prefix = colon == std::string::npos ? "" : value.substr(0, colon);
		auto local = colon == std::string::npos ? value : value.substr(colon + 1);
		auto uri = ns[prefix];
		std::string tag = "unknown";
		if (uri == Schema)
			tag = "tt";
		else if (uri == Analytics)
			tag = "tan";
		else if (uri == Rules)
			tag = "axt";
		else if (uri == "http://www.w3.org/2003/05/soap-envelope")
			tag = "s";
		return tag + ":" + local;
	};
	Tree result;
	result.data() = input.data();
	for (const auto& [key, value] : input)
	{
		if (key == "<xmlattr>")
		{
			for (const auto& [attr, content] : value)
				if (attr != "xmlns" && !attr.starts_with("xmlns:"))
					result.put("<xmlattr>." + attr, attr == "Type" ? qname(content.data()) : content.data());
		}
		else if (key != "<xmlcomment>")
		{
			auto outer = ns;
			if (auto attrs = value.get_child_optional("<xmlattr>"))
				for (const auto& [attr, content] : *attrs)
					if (attr == "xmlns")
						ns[""] = content.data();
					else if (attr.starts_with("xmlns:"))
						ns[attr.substr(6)] = content.data();
			auto child = canonical(value, ns);
			if (qname(key) == "tan:RuleType")
				child.data() = qname(value.data());
			result.add_child(qname(key), child);
			ns = std::move(outer);
		}
	}
	return result;
}

Tree& config(Tree& profiles, const std::string& token)
{
	for (auto& [_, node] : profiles.get_child("Analytics"))
		if (node.get<std::string>("token") == token)
			return node;
	throw RuleFault("NoConfig");
}

const Tree& source(const Tree& profiles, const std::string& token)
{
	for (const auto& [_, node] : profiles.get_child("VideoSource"))
		for (const auto& [__, compatible] : node.get_child("CompatibleAnalyticsConfigurations"))
			if (compatible.data() == token)
				return node;
	throw RuleFault("ConfigurationConflict", true);
}

Polygon polygon(const Tree& points)
{
	Polygon result;
	for (const auto& [_, point] : points)
		bg::append(result.outer(), Point(point.get<double>("x"), point.get<double>("y")));
	bg::correct(result);
	return result;
}

Tree defaults(const Tree& src)
{
	Tree result, points;
	result.put("Armed", true);
	result.put("Sensitivity", 0.5);
	auto x = src.get<double>("Bounds.x"), y = src.get<double>("Bounds.y");
	auto w = src.get<double>("Bounds.width"), h = src.get<double>("Bounds.height");
	for (const auto& [px, py] : {std::pair{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}})
	{
		Tree point;
		point.put("x", px);
		point.put("y", py);
		points.push_back({"", point});
	}
	result.add_child("Polygon", points);
	return result;
}

Tree readRule(const Tree& xml, const Tree& src, const Tree* previous)
{
	if (xml.get<std::string>("<xmlattr>.Type") != "tt:MotionRegionDetector")
		throw RuleFault("InvalidRule");
	if (xml.count("tt:Parameters") != 1 || xml.size() != 2)
		throw RuleFault("InvalidRule");
	Tree result;
	result.put("Name", xml.get<std::string>("<xmlattr>.Name"));
	if (result.get<std::string>("Name").empty())
		throw RuleFault("InvalidRule");
	result.put("Type", "tt:MotionRegionDetector");
	Tree region = previous ? previous->get_child("MotionRegion") : defaults(src);
	const auto& params = xml.get_child("tt:Parameters");
	if (params.size() > 1)
		throw RuleFault("InvalidRule");
	if (!params.empty())
	{
		const auto& [key, item] = params.front();
		if (key != "tt:ElementItem" || item.get<std::string>("<xmlattr>.Name") != "MotionRegion")
			throw RuleFault("InvalidRule");
		const auto& value = item.get_child("axt:MotionRegionConfig");
		if (item.size() != 2)
			throw RuleFault("InvalidRule"); // attributes and exactly one payload
		if (auto armed = value.get_optional<std::string>("<xmlattr>.Armed"))
		{
			if (*armed != "true" && *armed != "false" && *armed != "1" && *armed != "0")
				throw RuleFault("InvalidRule");
			region.put("Armed", *armed == "true" || *armed == "1");
		}
		if (auto sensitivity = value.get_optional<double>("<xmlattr>.Sensitivity"))
			region.put("Sensitivity", *sensitivity);
		else if (value.get_optional<std::string>("<xmlattr>.Sensitivity"))
			throw RuleFault("InvalidRule");
		if (auto points = value.get_child_optional("tt:Polygon"))
		{
			Tree list;
			for (const auto& [pointKey, point] : *points)
			{
				if (pointKey != "tt:Point")
					throw RuleFault("InvalidRule");
				Tree p;
				p.put("x", point.get<double>("<xmlattr>.x"));
				p.put("y", point.get<double>("<xmlattr>.y"));
				list.push_back({"", p});
			}
			region.put_child("Polygon", list);
		}
		if (value.count("tt:Polygon") > 1 || value.count("axt:PresetToken"))
			throw RuleFault("InvalidRule");
		for (const auto& [child, _] : value)
			if (child != "<xmlattr>" && child != "tt:Polygon")
				throw RuleFault("InvalidRule");
	}
	auto sensitivity = region.get<double>("Sensitivity");
	if (!std::isfinite(sensitivity) || sensitivity < 0 || sensitivity > 1)
		throw RuleFault("InvalidRule");
	const auto& points = region.get_child("Polygon");
	if (points.size() < 3 || points.size() > 8)
		throw RuleFault("InvalidRule");
	for (const auto& [_, p] : points)
	{
		auto x = p.get<double>("x"), y = p.get<double>("y");
		if (!std::isfinite(x) || !std::isfinite(y) || x < src.get<double>("Bounds.x") || y < src.get<double>("Bounds.y") ||
				x > src.get<double>("Bounds.x") + src.get<double>("Bounds.width") ||
				y > src.get<double>("Bounds.y") + src.get<double>("Bounds.height"))
			throw RuleFault("InvalidRule");
	}
	if (!bg::is_valid(polygon(points)))
		throw RuleFault("InvalidRule");
	result.add_child("MotionRegion", region);
	return result;
}
} // namespace

MotionRegionDetector::MotionRegionDetector(const Tree& profiles, SaveRules save)
		: profiles_(profiles), save_(std::move(save))
{
	size_t count = 0;
	for (auto& [_, cfg] : profiles_.get_child("Analytics"))
	{
		if (!cfg.get_child_optional("rules"))
			cfg.add_child("rules", Tree{});
		std::set<std::string> names;
		for (auto& [__, rule] : cfg.get_child("rules"))
		{
			if (!names.insert(rule.get<std::string>("Name")).second)
				throw RuleFault("RuleAlreadyExistent");
			rule = readRule(RuleXml(rule), source(profiles_, cfg.get<std::string>("token")), nullptr);
			++count;
		}
	}
	if (count > 4)
		throw RuleFault("TooManyRules", true);
}

MotionRegionDetector::Tree MotionRegionDetector::RuleXml(const Tree& rule)
{
	Tree result, points;
	result.put("<xmlattr>.Name", rule.get<std::string>("Name"));
	result.put("<xmlattr>.Type", rule.get<std::string>("Type"));
	const auto& region = rule.get_child("MotionRegion");
	for (const auto& [_, point] : region.get_child("Polygon"))
	{
		Tree p;
		p.put("<xmlattr>.x", point.get<double>("x"));
		p.put("<xmlattr>.y", point.get<double>("y"));
		points.add_child("tt:Point", p);
	}
	Tree value;
	value.put("<xmlattr>.Armed", region.get<bool>("Armed"));
	value.put("<xmlattr>.Sensitivity", region.get<double>("Sensitivity"));
	value.add_child("tt:Polygon", points);
	result.put("tt:Parameters.tt:ElementItem.<xmlattr>.Name", "MotionRegion");
	result.add_child("tt:Parameters.tt:ElementItem.axt:MotionRegionConfig", value);
	return result;
}

MotionRegionDetector::Tree MotionRegionDetector::Execute(const std::string& operation, const std::string& xml)
{
	std::lock_guard lock(mutex_);
	try
	{
		auto document = canonical(exns::to_ptree(xml));
		if (document.size() != 1 || document.count("s:Envelope") != 1)
			throw RuleFault("InvalidRule");
		const auto& envelope = document.get_child("s:Envelope");
		if (envelope.count("s:Body") != 1 || envelope.get_child("s:Body").size() != 1)
			throw RuleFault("InvalidRule");
		const auto& request = document.get_child("s:Envelope.s:Body.tan:" + operation);
		if (request.count("tan:ConfigurationToken") != 1)
			throw RuleFault("NoConfig");
		bool tokenSeen = false;
		for (const auto& [key, child] : request)
		{
			if (key == "<xmlattr>")
				continue;
			if (key == "tan:ConfigurationToken")
			{
				tokenSeen = true;
				continue;
			}
			if (operation == "GetRuleOptions" && key == "tan:RuleType" && !tokenSeen && request.count(key) == 1)
				continue;
			if ((operation == "CreateRules" || operation == "ModifyRules") && key == "tan:Rule" && tokenSeen)
				continue;
			if (operation == "DeleteRules" && key == "tan:RuleName" && tokenSeen)
				continue;
			throw RuleFault("InvalidRule");
		}
		auto token = request.get<std::string>("tan:ConfigurationToken");
		auto& cfg = config(profiles_, token);
		auto& current = cfg.get_child("rules");
		Tree response;
		if (operation == "GetRules")
		{
			for (const auto& [_, rule] : current)
				response.add_child("tan:Rule", RuleXml(rule));
			return response;
		}
		if (operation == "GetSupportedRules")
		{
			Tree desc;
			desc.put("<xmlattr>.Name", "tt:MotionRegionDetector");
			desc.put("<xmlattr>.fixed", false);
			desc.put("<xmlattr>.maxInstances", 4);
			desc.put("tt:Parameters.tt:ElementItemDescription.<xmlattr>.Name", "MotionRegion");
			desc.put("tt:Parameters.tt:ElementItemDescription.<xmlattr>.Type", "axt:MotionRegionConfig");
			desc.put("tt:Messages.<xmlattr>.IsProperty", true);
			for (const auto& [name, type] : {std::pair{"VideoSource", "tt:ReferenceToken"}, {"RuleName", "xs:string"}})
			{
				Tree item;
				item.put("<xmlattr>.Name", name);
				item.put("<xmlattr>.Type", type);
				desc.add_child("tt:Messages.tt:Source.tt:SimpleItemDescription", item);
			}
			desc.put("tt:Messages.tt:Key", "");
			desc.put("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Name", "State");
			desc.put("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Type", "xs:boolean");
			desc.put("tt:Messages.tt:ParentTopic", Topic);
			response.put("tan:SupportedRules.tt:RuleContentSchemaLocation", "http://www.onvif.org/ver20/analytics/rules.xsd");
			response.add_child("tan:SupportedRules.tt:RuleDescription", desc);
			return response;
		}
		if (operation == "GetRuleOptions")
		{
			if (request.get<std::string>("tan:RuleType", "tt:MotionRegionDetector") != "tt:MotionRegionDetector")
				throw RuleFault("RuleNotExistent");
			Tree options;
			options.put("<xmlattr>.Name", "MotionRegion");
			options.put("<xmlattr>.RuleType", "tt:MotionRegionDetector");
			options.put("<xmlattr>.Type", "axt:MotionRegionConfigOptions");
			const std::string path = "axt:MotionRegionConfigOptions.";
			options.put(path + "<xmlattr>.PTZPresetMotionSupport", false);
			options.put(path + "axt:DisarmSupport", true);
			options.put(path + "axt:PolygonSupport", true);
			options.put(path + "axt:PolygonLimits.tt:Min", 3);
			options.put(path + "axt:PolygonLimits.tt:Max", 8);
			options.put(path + "axt:SingleSensitivitySupport", false);
			options.put(path + "axt:RuleNotification", true);
			response.add_child("tan:RuleOptions", options);
			return response;
		}
		auto candidate = current;
		std::set<std::string> names;
		std::deque<Message> events;
		for (const auto& [key, item] : request)
		{
			if (key == "<xmlattr>" || key == "tan:ConfigurationToken")
				continue;
			bool deleting = operation == "DeleteRules";
			if (key != (deleting ? "tan:RuleName" : "tan:Rule"))
				throw RuleFault("InvalidRule");
			auto name = deleting ? item.data() : item.get<std::string>("<xmlattr>.Name");
			if (!names.insert(name).second)
				throw RuleFault("InvalidRule");
			auto it = std::find_if(candidate.begin(), candidate.end(),
														 [&](const auto& n) { return n.second.template get<std::string>("Name") == name; });
			bool exists = it != candidate.end();
			if (operation == "CreateRules" && exists)
				throw RuleFault("RuleAlreadyExistent");
			if (operation != "CreateRules" && !exists)
				throw RuleFault("RuleNotExistent");
			if (deleting)
			{
				events.push_back(Notification(token, it->second, "Deleted", false));
				candidate.erase(it);
			}
			else
			{
				auto rule = readRule(item, source(profiles_, token), exists ? &it->second : nullptr);
				if (exists)
					it->second = rule;
				else
					candidate.push_back({"", rule});
				events.push_back(Notification(token, rule, exists ? "Changed" : "Initialized", false));
			}
		}
		if (names.empty())
			throw RuleFault("InvalidRule");
		size_t count = candidate.size();
		for (const auto& [_, other] : profiles_.get_child("Analytics"))
			if (other.get<std::string>("token") != token)
				count += other.get_child("rules").size();
		if (count > 4)
			throw RuleFault("TooManyRules", true);
		// Commit only after validation of the complete batch and successful persistence.
		try
		{
			save_(token, candidate);
		}
		catch (const std::exception&)
		{
			throw RuleFault("ConfigurationConflict", true);
		}
		current.swap(candidate);
		for (const auto& name : names)
			states_.erase({token, name});
		pending_.insert(pending_.end(), events.begin(), events.end());
		return response;
	}
	catch (const boost::property_tree::ptree_error&)
	{
		throw RuleFault("InvalidRule");
	}
}

MotionRegionDetector::Message MotionRegionDetector::Notification(const std::string& token, const Tree& rule,
																																 const std::string& operation, bool state) const
{
	Message msg;
	msg.topic = Topic;
	msg.utc_time = datetime::system_utc_datetime();
	msg.property_operation = operation;
	msg.source_item_descriptions = {{"VideoSource", source(profiles_, token).get<std::string>("SourceToken")},
																	{"RuleName", rule.get<std::string>("Name")}};
	msg.data_name = "State";
	msg.data_value = state ? "true" : "false";
	return msg;
}

std::deque<MotionRegionDetector::Message> MotionRegionDetector::Synchronize() const
{
	std::lock_guard lock(mutex_);
	std::deque<Message> result;
	for (const auto& [_, cfg] : profiles_.get_child("Analytics"))
		for (const auto& [__, rule] : cfg.get_child("rules"))
		{
			auto token = cfg.get<std::string>("token");
			auto it = states_.find({token, rule.get<std::string>("Name")});
			result.push_back(Notification(token, rule, "Initialized", it != states_.end() && it->second));
		}
	return result;
}

std::deque<MotionRegionDetector::Message> MotionRegionDetector::Sample(double x, double y, double strength)
{
	std::lock_guard lock(mutex_);
	std::deque<Message> result;
	result.swap(pending_);
	for (const auto& [_, cfg] : profiles_.get_child("Analytics"))
		for (const auto& [__, rule] : cfg.get_child("rules"))
		{
			auto token = cfg.get<std::string>("token");
			const auto& region = rule.get_child("MotionRegion");
			bool next = region.get<bool>("Armed") && strength > 0 && strength >= 1 - region.get<double>("Sensitivity") &&
									bg::covered_by(Point(x, y), polygon(region.get_child("Polygon")));
			auto& state = states_[{token, rule.get<std::string>("Name")}];
			if (state != next)
				result.push_back(Notification(token, rule, "Changed", next));
			state = next;
		}
	return result;
}
} // namespace utility
