#include "MetadataConfiguration.h"
#include "../Server.h"
#include "MediaProfilesManager.h"
#include <algorithm>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <cctype>
#include <glib.h>
#include <sstream>

namespace osrv::metadata
{
namespace
{
namespace pt = boost::property_tree;
const pt::ptree Empty;
using Namespaces = std::map<std::string, std::string>;
constexpr const char* Schema = "http://www.onvif.org/ver10/schema";
constexpr const char* Wsnt = "http://docs.oasis-open.org/wsn/b-2";
constexpr const char* Concrete = "http://docs.oasis-open.org/wsn/t-1/TopicExpression/Concrete";
constexpr const char* ConcreteSet = "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet";
constexpr const char* ItemFilter = "http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter";
std::string local(const std::string& name)
{
	return name.substr(name.find(':') == std::string::npos ? 0 : name.find(':') + 1);
}
void bindings(const pt::ptree& node, Namespaces& ns)
{
	for (const auto& [key, value] : node.get_child("<xmlattr>", Empty))
		if (key == "xmlns")
			ns[""] = value.data();
		else if (key.starts_with("xmlns:"))
			ns[key.substr(6)] = value.data();
}
std::string expanded(const std::string& name, const Namespaces& ns, const std::string& fallback = "")
{
	auto colon = name.find(':');
	auto prefix = colon == std::string::npos ? "" : name.substr(0, colon);
	auto it = ns.find(prefix);
	if (it == ns.end() && (colon != std::string::npos || fallback.empty()))
		throw Fault("ConfigModify", "Unbound namespace in metadata filter");
	return "{" + (it == ns.end() ? fallback : it->second) + "}" + local(name);
}
const pt::ptree* optional(const pt::ptree& node, const std::string& key)
{
	const pt::ptree* found = nullptr;
	for (const auto& [name, value] : node)
		if (local(name) == key)
		{
			if (found)
				throw Fault("ConfigModify", "Duplicate metadata parameter: " + key);
			found = &value;
		}
	return found;
}
const pt::ptree& child(const pt::ptree& node, const std::string& key)
{
	if (auto value = optional(node, key))
		return *value;
	throw Fault("ConfigModify", "Missing metadata parameter: " + key);
}
void fields(const pt::ptree& node, Namespaces ns, const std::vector<std::string>& order)
{
	bindings(node, ns);
	size_t previous = 0;
	std::map<std::string, unsigned> counts;
	for (const auto& [key, value] : node)
	{
		if (key == "<xmlattr>")
			continue;
		auto scope = ns;
		bindings(value, scope);
		auto position = std::ranges::find(order, local(key));
		auto index = static_cast<size_t>(position - order.begin());
		if (position == order.end() || expanded(key, scope) != std::string("{") + Schema + "}" + local(key) ||
				index < previous || ++counts[local(key)] > 1)
			throw Fault("ConfigModify", "Invalid metadata field name, namespace, order or cardinality");
		previous = index;
	}
}
std::string text(const pt::ptree& node, const std::string& key, const std::string& fallback = "")
{
	auto value = optional(node, key);
	return value ? value->data() : fallback;
}
bool boolean(const std::string& value)
{
	if (value == "true" || value == "1")
		return true;
	if (value == "false" || value == "0")
		return false;
	throw Fault("ConfigModify", "Invalid boolean in metadata configuration");
}
struct ContentItem
{
	std::string type, name, value;
};
using Items = std::vector<ContentItem>;
using ItemTest = std::function<bool(const ContentItem&)>;
using ContentTest = std::function<bool(const Items&)>;
// The ItemFilter grammar is the bounded XPath subset defined in Core 24.12.
// Parsing it directly avoids accepting unsupported XPath functions or axes.
class ContentParser
{
	const std::string& input_;
	const Namespaces& ns_;
	size_t pos_ = 0;
	unsigned depth_ = 0;
	void space()
	{
		while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_])))
			++pos_;
	}
	bool eat(std::string_view token)
	{
		space();
		if (input_.compare(pos_, token.size(), token) != 0)
			return false;
		if (std::isalpha(static_cast<unsigned char>(token.back())) && pos_ + token.size() < input_.size() &&
				(std::isalnum(static_cast<unsigned char>(input_[pos_ + token.size()])) || input_[pos_ + token.size()] == '_'))
			return false;
		pos_ += token.size();
		return true;
	}
	void require(std::string_view token)
	{
		if (!eat(token))
			throw Fault("ConfigModify", "Invalid ItemFilter expression");
	}
	std::string literal()
	{
		space();
		if (pos_ == input_.size() || (input_[pos_] != '\'' && input_[pos_] != '"'))
			throw Fault("ConfigModify", "Expected filter string literal");
		auto quote = input_[pos_++];
		auto start = pos_;
		auto end = input_.find(quote, start);
		if (end == std::string::npos)
			throw Fault("ConfigModify", "Unterminated filter literal");
		pos_ = end + 1;
		return input_.substr(start, end - start);
	}
	template <class Argument> using Test = std::function<bool(const Argument&)>;
	template <class Argument> Test<Argument> expression(bool attributes, bool conjunction = false)
	{
		auto result = atom<Argument>(attributes);
		while (true)
		{
			if (eat("and"))
			{
				auto right = atom<Argument>(attributes);
				auto left = result;
				result = [left, right](const Argument& value) { return left(value) && right(value); };
			}
			else if (!conjunction && eat("or"))
			{
				auto right = expression<Argument>(attributes, true);
				auto left = result;
				result = [left, right](const Argument& value) { return left(value) || right(value); };
			}
			else
				return result;
		}
	}
	template <class Argument> Test<Argument> atom(bool attributes)
	{
		if (++depth_ > 2048)
			throw Fault("ConfigModify", "Metadata filter nesting limit exceeded");
		Test<Argument> result;
		if (eat("not"))
		{
			require("(");
			auto nested = expression<Argument>(attributes);
			require(")");
			result = [nested](const Argument& value) { return !nested(value); };
		}
		else if (eat("("))
		{
			result = expression<Argument>(attributes);
			require(")");
		}
		else if constexpr (std::is_same_v<Argument, ContentItem>)
		{
			bool name = eat("@Name");
			if (!name)
				require("@Value");
			require("=");
			auto expected = literal();
			result = [name, expected](const ContentItem& item) {
				return (name || item.type == "SimpleItem") && (name ? item.name : item.value) == expected;
			};
		}
		else
		{
			require("boolean");
			require("(");
			require("//");
			space();
			auto start = pos_;
			while (pos_ < input_.size() && (static_cast<unsigned char>(input_[pos_]) >= 128 ||
																			std::isalnum(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_' ||
																			input_[pos_] == ':' || input_[pos_] == '-' || input_[pos_] == '.'))
				++pos_;
			auto item = input_.substr(start, pos_ - start);
			auto type = expanded(item, ns_, Schema);
			if (type != std::string("{") + Schema + "}SimpleItem" && type != std::string("{") + Schema + "}ElementItem")
				throw Fault("ConfigModify", "Invalid ItemFilter item namespace or name");
			require("[");
			auto test = expression<ContentItem>(true);
			require("]");
			require(")");
			result = [test, type = local(item)](const Items& items) {
				return std::ranges::any_of(items, [&](const auto& item) { return item.type == type && test(item); });
			};
		}
		--depth_;
		return result;
	}

public:
	ContentParser(const std::string& input, const Namespaces& ns) : input_(input), ns_(ns)
	{
	}
	ContentTest Parse()
	{
		auto result = expression<Items>(false);
		space();
		if (pos_ != input_.size())
			throw Fault("ConfigModify", "Unsupported ItemFilter syntax");
		return result;
	}
};
bool name(const std::string& value)
{
	if (value.empty())
		return false;
	auto first = static_cast<unsigned char>(value.front());
	return (first >= 128 || std::isalpha(first) || first == '_') && std::ranges::all_of(value, [](unsigned char c) {
					 return c >= 128 || std::isalnum(c) || c == '_' || c == '-' || c == '.';
				 });
}
std::vector<std::string> topic(const std::string& expression, const Namespaces& ns)
{
	std::vector<std::string> result;
	std::istringstream parts(expression);
	std::string part, rootNamespace;
	while (std::getline(parts, part, '/'))
	{
		auto colon = part.find(':');
		if ((colon == std::string::npos && !name(part)) ||
				(colon != std::string::npos && (!name(part.substr(0, colon)) || !name(part.substr(colon + 1)))))
			throw Fault("ConfigModify", "Invalid topic QName");
		auto value = !result.empty() && part.find(':') == std::string::npos ? "{" + rootNamespace + "}" + part
																																				: expanded(part, ns, rootNamespace);
		if (result.empty())
			rootNamespace = value.substr(1, value.find('}') - 1);
		result.push_back(value);
	}
	if (result.empty() || expression.ends_with('/'))
		throw Fault("ConfigModify", "Empty topic filter or trailing slash");
	return result;
}
std::string path(const std::string& token)
{
	// A digest keeps even maximum-length reference tokens below Media2's 128-octet URI limit.
	auto digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, token.data(), token.size());
	auto result = std::string("/onvif/profile/") + digest;
	g_free(digest);
	return result;
}
} // namespace

std::shared_ptr<const Settings> Compile(const pt::ptree& config)
{
	auto settings = std::make_shared<Settings>();
	settings->events = config.get<bool>("Events", false);
	std::ostringstream identity;
	pt::write_json(identity, config);
	settings->identity = identity.str();
	using EventTest = std::function<bool(const event::NotificationMessage&, const Namespaces&)>;
	std::vector<EventTest> tests;
	for (const auto& [_, filter] : config.get_child("Filters", Empty))
	{
		auto expression = filter.get<std::string>("Expression");
		auto dialect = filter.get<std::string>("Dialect");
		if (expression.empty() || expression.size() > 4096)
			throw Fault("ConfigModify", "Metadata filter size must be 1..4096 bytes");
		Namespaces ns;
		for (const auto& [prefix, uri] : filter.get_child("Namespaces", Empty))
			ns[prefix] = uri.data();
		if (filter.get<std::string>("Kind") == "TopicExpression")
		{
			if (dialect != Concrete && dialect != ConcreteSet)
				throw Fault("ConfigModify", "Unsupported metadata topic dialect");
			std::vector<std::pair<std::vector<std::string>, bool>> paths;
			std::istringstream alternatives(expression);
			std::string part;
			while (std::getline(alternatives, part, '|'))
			{
				auto first = part.find_first_not_of(" \r\n\t");
				auto last = part.find_last_not_of(" \r\n\t");
				if (first == std::string::npos)
					throw Fault("ConfigModify", "Empty topic alternative");
				part = part.substr(first, last - first + 1);
				bool subtree = part.ends_with("//.");
				if ((subtree || !paths.empty()) && dialect == Concrete)
					throw Fault("ConfigModify", "Concrete requires one exact topic path");
				if (subtree)
					part.resize(part.size() - 3);
				paths.emplace_back(topic(part, ns), subtree);
			}
			if (expression.ends_with('|') || paths.empty())
				throw Fault("ConfigModify", "Invalid topic expression");
			tests.push_back([paths](const auto& message, const auto& sourceNamespaces) {
				try
				{
					auto actual = topic(message.topic, sourceNamespaces);
					return std::ranges::any_of(paths, [&](const auto& rule) {
						return actual.size() >= rule.first.size() && (rule.second || actual.size() == rule.first.size()) &&
									 std::equal(rule.first.begin(), rule.first.end(), actual.begin());
					});
				}
				catch (const Fault&)
				{
					return false;
				}
			});
		}
		else if (filter.get<std::string>("Kind") == "MessageContent")
		{
			if (dialect != ItemFilter)
				throw Fault("ConfigModify", "Unsupported metadata content dialect");
			auto test = ContentParser(expression, ns).Parse();
			tests.push_back([test](const auto& message, const auto&) {
				Items items;
				for (const auto& [name, value] : message.source_item_descriptions)
					items.push_back({"SimpleItem", name, value});
				if (!message.data_name.empty())
					items.push_back({"SimpleItem", message.data_name, message.data_value});
				for (const auto& [name, value] : message.data_simple_items)
					items.push_back({"SimpleItem", name, value});
				for (const auto& item : message.data_element_items)
					items.push_back({"ElementItem", item.template get<std::string>("<xmlattr>.Name", ""), ""});
				return test(items);
			});
		}
		else
			throw Fault("ConfigModify", "Unsupported metadata filter element");
	}
	settings->accepts = [enabled = settings->events, tests](const auto& message, const auto& ns) {
		return enabled && std::ranges::all_of(tests, [&](const auto& test) { return test(message, ns); });
	};
	return settings;
}

pt::ptree ConfigurationXml(const pt::ptree& config, const pt::ptree& profiles)
{
	pt::ptree out;
	auto token = config.get<std::string>("token");
	out.put("<xmlattr>.token", token);
	out.put("<xmlattr>.CompressionType", "None");
	out.put("tt:Name", config.get<std::string>("Name"));
	unsigned count = 0;
	for (const auto& [_, profile] : profiles.get_child("MediaProfiles"))
		if (profile.get<std::string>("Metadata", "") == token)
			++count;
	out.put("tt:UseCount", count);
	out.put("tt:PTZStatus.tt:Status", false);
	out.put("tt:PTZStatus.tt:Position", false);
	if (config.get<bool>("Events", false))
	{
		pt::ptree events;
		for (const auto& [_, filter] : config.get_child("Filters", Empty))
		{
			pt::ptree expression;
			expression.put_value(filter.get<std::string>("Expression"));
			expression.put("<xmlattr>.Dialect", filter.get<std::string>("Dialect"));
			for (const auto& [prefix, uri] : filter.get_child("Namespaces", Empty))
				expression.get_child("<xmlattr>")
						.push_back({prefix.empty() ? "xmlns" : "xmlns:" + prefix, pt::ptree(uri.data())});
			auto prefix = std::string("filter");
			while (expression.get_child("<xmlattr>").find("xmlns:" + prefix) != expression.get_child("<xmlattr>").not_found())
				prefix += "_";
			expression.get_child("<xmlattr>").push_back({"xmlns:" + prefix, pt::ptree(Wsnt)});
			events.add_child("tt:Filter." + prefix + ":" + filter.get<std::string>("Kind"), expression);
		}
		out.add_child("tt:Events", events);
	}
	out.put("tt:Analytics", false);
	out.put("tt:Multicast.tt:Address.tt:Type", "IPv4");
	out.put("tt:Multicast.tt:Address.tt:IPv4Address", "0.0.0.0");
	out.put("tt:Multicast.tt:Port", 0);
	out.put("tt:Multicast.tt:TTL", 1);
	out.put("tt:Multicast.tt:AutoStart", false);
	out.put("tt:SessionTimeout", "PT60S");
	return out;
}

Control::Control(utility::media::MediaProfilesManager& manager, const ServerConfigs& server,
								 const std::string& configs_path)
		: manager_(manager), server_(server)
{
	if (configs_path.empty())
		encoders_ = {{"VideoEncoderToken0", "VideoEncoderToken0"}, {"VideoEncoderToken1", "VideoEncoderToken1"}};
	else
		for (const auto& file : {"media.config", "media2.config"})
		{
			pt::ptree service;
			pt::read_json(configs_path + "/" + file, service);
			for (const auto& [_, route] : service.get_child("GetStreamUri"))
			{
				auto mount = route.get<std::string>("Uri");
				if (mount != "Live&HighStream" && mount != "Live&LowStream")
					continue;
				auto token = route.get<std::string>("VideoEncoderToken");
				auto variant = mount == "Live&HighStream" ? "VideoEncoderToken0" : "VideoEncoderToken1";
				if (auto found = encoders_.find(token); found != encoders_.end() && found->second != variant)
					throw std::runtime_error("Media and Media2 map the same video encoder token to different live streams");
				encoders_[token] = variant;
			}
		}
}
void Control::Bind(Configure configure, Synchronize synchronize, Retain retain)
{
	configure_ = std::move(configure);
	synchronize_ = std::move(synchronize);
	retain_ = std::move(retain);
}
void Control::Refresh()
{
	if (!configure_)
		return;
	std::vector<std::string> routes;
	for (const auto& [_, profile] : manager_.ReaderWriter()->ConfigsTree().get_child("MediaProfiles"))
	{
		if (profile.get<std::string>("VideoEncoder", "").empty() && profile.get<std::string>("Metadata", "").empty())
			continue;
		auto token = profile.get<std::string>("token");
		if (auto encoder = profile.get<std::string>("VideoEncoder", ""); !encoder.empty() && !encoders_.contains(encoder))
			continue;
		Uri(token);
		routes.push_back(path(token));
	}
	if (retain_)
		retain_(routes);
}

std::string Control::Uri(const std::string& token)
{
	const auto& profile = manager_.GetProfileByToken(token);
	auto encoder = profile.get<std::string>("VideoEncoder", "");
	if (!encoder.empty() && !encoders_.contains(encoder))
		throw incomplete_configuration();
	auto metadata = profile.get<std::string>("Metadata", "");
	if (encoder.empty() && metadata.empty())
		throw incomplete_configuration();
	std::shared_ptr<const Settings> settings;
	if (!metadata.empty())
		settings = Compile(manager_.GetConfigByToken(metadata, "Metadata"));
	if (!configure_)
		throw std::runtime_error("RTSP profile control is unavailable");
	configure_(path(token), encoder.empty() ? "" : encoders_.at(encoder),
						 !profile.get<std::string>("AudioSource", "").empty() &&
								 !profile.get<std::string>("AudioEncoder", "").empty(),
						 settings);
	return server_.RtspAddress() + path(token).substr(1);
}

pt::ptree Control::Execute(const std::string& operation, const std::string& xml, bool media2)
{
	if (!server_.rtsp_metadata_enabled_)
		throw Fault("ConfigModify", "RTSP metadata is disabled by server configuration");
	pt::ptree document;
	std::istringstream input(xml);
	pt::read_xml(input, document, pt::xml_parser::trim_whitespace);
	Namespaces ns;
	const auto* node = &document;
	auto serviceNamespace =
			std::string(media2 ? "http://www.onvif.org/ver20/media/wsdl" : "http://www.onvif.org/ver10/media/wsdl");
	for (const auto& level : {std::string("Envelope"), std::string("Body"), operation})
	{
		const auto& selected = child(*node, level);
		bindings(selected, ns);
		for (const auto& [name, value] : *node)
			if (&value == &selected &&
					expanded(name, ns) !=
							"{" + (level == operation ? serviceNamespace : "http://www.w3.org/2003/05/soap-envelope") + "}" + level)
				throw Fault("ConfigModify", "Invalid SOAP or media operation namespace");
		node = &selected;
	}
	const auto& request = *node;
	std::vector<std::string> order;
	if (operation == "SetMetadataConfiguration")
		order = media2 ? std::vector<std::string>{"Configuration"}
									 : std::vector<std::string>{"Configuration", "ForcePersistence"};
	else if (operation == "GetStreamUri")
		order = {media2 ? "Protocol" : "StreamSetup", "ProfileToken"};
	else if (operation == "AddMetadataConfiguration")
		order = {"ProfileToken", "ConfigurationToken"};
	else if (operation == "RemoveMetadataConfiguration" || operation == "SetSynchronizationPoint" ||
					 operation == "GetCompatibleMetadataConfigurations")
		order = {"ProfileToken"};
	else if (operation == "GetMetadataConfiguration")
		order = {"ConfigurationToken"};
	else if (operation != "GetMetadataConfigurations" || media2)
		order = {"ConfigurationToken", "ProfileToken"};
	size_t previous = 0;
	std::map<std::string, unsigned> counts;
	for (const auto& [key, value] : request)
	{
		if (key == "<xmlattr>")
			continue;
		auto scope = ns;
		bindings(value, scope);
		auto position = std::ranges::find(order, local(key));
		auto index = static_cast<size_t>(position - order.begin());
		if (position == order.end() || expanded(key, scope) != "{" + serviceNamespace + "}" + local(key) ||
				index < previous || ++counts[local(key)] > 1)
			throw Fault("ConfigModify", "Invalid media request parameter namespace, order or cardinality");
		previous = index;
	}
	pt::ptree result;
	auto prefix = media2 ? "tr2:" : "trt:";
	auto token = text(request, "ConfigurationToken");
	auto profile = text(request, "ProfileToken");
	if (operation == "GetCompatibleMetadataConfigurations" && profile.empty())
		throw no_such_profile();
	if (optional(request, "ConfigurationToken"))
		manager_.GetConfigByToken(token, "Metadata");
	bool compatible = true;
	if (!profile.empty())
	{
		const auto& selected = manager_.GetProfileByToken(profile);
		compatible =
				!selected.get<std::string>("VideoSource", "").empty() || !selected.get<std::string>("AudioSource", "").empty();
	}
	if (operation == "GetStreamUri")
	{
		if (media2)
		{
			auto protocol = text(request, "Protocol");
			if (protocol != "RTSP" && protocol != "RtspUnicast")
				throw Fault("InvalidStreamSetup", "Only RTSP TCP and UDP unicast are supported");
		}
		else
		{
			auto setup = optional(request, "StreamSetup");
			if (!setup)
				throw Fault("InvalidStreamSetup", "Missing stream setup");
			auto transport = optional(*setup, "Transport");
			auto protocol = transport ? text(*transport, "Protocol") : "";
			if (text(*setup, "Stream") != "RTP-Unicast" || (protocol != "UDP" && protocol != "TCP" && protocol != "RTSP") ||
					(transport && optional(*transport, "Tunnel")))
				throw Fault("InvalidStreamSetup", "Only RTSP TCP and UDP unicast are supported");
		}

		if (profile.empty())
			throw no_such_profile();
		auto uri = Uri(profile);
		if (media2)
			result.put("tr2:Uri", uri);
		else
		{
			result.put("trt:MediaUri.tt:Uri", uri);
			result.put("trt:MediaUri.tt:InvalidAfterConnect", false);
			result.put("trt:MediaUri.tt:InvalidAfterReboot", false);
			result.put("trt:MediaUri.tt:Timeout", "PT0S");
		}
	}
	else if (operation == "SetSynchronizationPoint")
	{
		if (profile.empty())
			throw no_such_profile();
		if (synchronize_)
			synchronize_(path(profile));
	}
	else if (operation == "AddMetadataConfiguration" || operation == "RemoveMetadataConfiguration")
	{
		if (profile.empty())
			throw no_such_profile();
		if (operation.starts_with("Add"))
			manager_.AddConfiguration(profile, "Metadata", token);
		else
			manager_.RemoveConfiguration(profile, "Metadata");
	}
	else if (operation == "SetMetadataConfiguration")
	{
		const auto& configuration = child(request, "Configuration");
		bindings(configuration, ns);
		token = configuration.get<std::string>("<xmlattr>.token", "");
		manager_.GetConfigByToken(token, "Metadata");
		child(configuration, "UseCount");
		child(configuration, "SessionTimeout");
		if (!media2)
			boolean(text(request, "ForcePersistence")); // Obsolete: all accepted settings persist.
		fields(configuration, ns,
					 {"Name", "UseCount", "PTZStatus", "Events", "Analytics", "Multicast", "SessionTimeout",
						"AnalyticsEngineConfiguration"});

		pt::ptree updated;
		updated.put("token", token);
		auto name = text(configuration, "Name");
		if (name.empty() || name.size() > 64)
			throw Fault("ConfigModify", "Metadata name must contain 1..64 bytes");
		updated.put("Name", name);
		for (const auto& [key, value] : configuration)
		{
			auto field = local(key);
			if (field == "Analytics" && boolean(value.data()))
				throw Fault("ConfigModify", "Analytics scene metadata is not supported");
			if (field == "PTZStatus")
				for (const auto& [flagName, flag] : value)
					if (flagName != "<xmlattr>" && boolean(flag.data()))
						throw Fault("ConfigModify", "PTZ metadata is not supported");
			if (field != "<xmlattr>" && field != "Name" && field != "UseCount" && field != "Analytics" &&
					field != "PTZStatus" && field != "Events" && field != "Multicast" && field != "SessionTimeout" &&
					field != "AnalyticsEngineConfiguration")
				throw Fault("ConfigModify", "Unsupported metadata configuration field");
		}
		for (const auto& [attribute, value] : configuration.get_child("<xmlattr>", Empty))
			if ((attribute == "CompressionType" && value.data() != "None") ||
					((attribute == "GeoLocation" || attribute == "ShapePolygon") && boolean(value.data())))
				throw Fault("ConfigModify", "Unsupported metadata encoding or scene option");
		const auto& multicast = child(configuration, "Multicast");
		const auto& address = child(multicast, "Address");
		fields(multicast, ns, {"Address", "Port", "TTL", "AutoStart"});
		Namespaces multicastNs = ns;
		bindings(multicast, multicastNs);
		fields(address, multicastNs, {"Type", "IPv4Address"});
		if (auto ptz = optional(configuration, "PTZStatus"))
		{
			fields(*ptz, ns, {"Status", "Position", "FieldOfView"});
			boolean(child(*ptz, "Status").data());
			boolean(child(*ptz, "Position").data());
		}
		if (text(address, "Type") != "IPv4" || text(address, "IPv4Address") != "0.0.0.0" ||
				text(multicast, "Port") != "0" || text(multicast, "TTL") != "1" || boolean(text(multicast, "AutoStart")))
			throw Fault("ConfigModify", "Only disabled multicast settings are supported");
		if (!media2 && text(configuration, "SessionTimeout") != "PT60S")
			throw Fault("ConfigModify", "Metadata session timeout is PT60S");
		auto events = optional(configuration, "Events");
		updated.put("Events", events != nullptr);
		if (events)
		{
			bindings(*events, ns);
			fields(*events, ns, {"Filter"});
			for (const auto& [key, value] : *events)
				if (key != "<xmlattr>" && local(key) != "Filter")
					throw Fault("ConfigModify", "Metadata subscription policies are not supported");
			if (auto filters = optional(*events, "Filter"))
			{
				bindings(*filters, ns);
				unsigned topics = 0, contents = 0;
				pt::ptree storedFilters;
				for (const auto& [key, value] : *filters)
				{
					if (key == "<xmlattr>")
						continue;
					auto filterNs = ns;
					bindings(value, filterNs);
					auto kind = local(key);
					for (const auto& [childName, _] : value)
						if (childName != "<xmlattr>")
							throw Fault("ConfigModify", "Filter expressions must be text");
					if (expanded(key, filterNs) != std::string("{") + Wsnt + "}" + kind ||
							(kind != "TopicExpression" && kind != "MessageContent") ||
							(kind == "TopicExpression" ? ++topics : ++contents) > 1)
						throw Fault("ConfigModify", "Invalid metadata filter element");
					pt::ptree stored, namespaces;
					stored.put("Kind", kind);
					stored.put("Dialect", value.get<std::string>("<xmlattr>.Dialect", ""));
					stored.put("Expression", value.data());
					for (const auto& [prefix, uri] : filterNs)
						namespaces.push_back({prefix, pt::ptree(uri)});
					stored.add_child("Namespaces", namespaces);
					storedFilters.push_back({"", stored});
				}
				if (!storedFilters.empty())
					updated.add_child("Filters", storedFilters);
			}
		}
		Compile(updated);
		auto& tree = manager_.ReaderWriter()->ConfigsTree();
		auto backup = tree;
		for (auto& [_, item] : tree.get_child("Metadata"))
			if (item.get<std::string>("token") == token)
				item = updated;
		try
		{
			manager_.ReaderWriter()->Save();
		}
		catch (...)
		{
			tree = backup;
			throw;
		}
	}
	else
	{
		if (operation == "GetMetadataConfiguration" && token.empty())
			throw no_config();
		if (!token.empty())
			manager_.GetConfigByToken(token, "Metadata");
		if (operation == "GetMetadataConfigurationOptions")
		{
			if (!compatible)
				throw no_config();
			pt::ptree options;
			options.put("<xmlattr>.MaxContentFilterSize", 4096);
			options.put("tt:PTZStatusFilterOptions.tt:PanTiltStatusSupported", false);
			options.put("tt:PTZStatusFilterOptions.tt:ZoomStatusSupported", false);
			options.put("tt:PTZStatusFilterOptions.tt:PanTiltPositionSupported", false);
			options.put("tt:PTZStatusFilterOptions.tt:ZoomPositionSupported", false);
			options.put("tt:Extension.tt:CompressionType", "None");
			result.add_child(std::string(prefix) + "Options", options);
		}
		else
			for (const auto& [_, item] : manager_.ReaderWriter()->ConfigsTree().get_child("Metadata", Empty))
				if (compatible && (token.empty() || item.get<std::string>("token") == token))
					result.add_child(std::string(prefix) +
															 (operation == "GetMetadataConfiguration" ? "Configuration" : "Configurations"),
													 ConfigurationXml(item, manager_.ReaderWriter()->ConfigsTree()));
	}
	return result;
}
} // namespace osrv::metadata
