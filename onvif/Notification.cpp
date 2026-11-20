#include "Notification.h"

#include <boost/property_tree/xml_parser.hpp>

#include <sstream>

namespace osrv::event
{
namespace pt = boost::property_tree;

pt::ptree serialize_notification(const NotificationMessage& message)
{
	pt::ptree node;
	node.put("wsnt:Topic", message.topic);
	node.put("wsnt:Topic.<xmlattr>.Dialect", "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
	pt::ptree body;
	body.put("<xmlattr>.UtcTime", message.utc_time);
	if (!message.property_operation.empty())
		body.put("<xmlattr>.PropertyOperation", message.property_operation);
	for (const auto& [name, value] : message.source_item_descriptions)
	{
		pt::ptree item;
		item.put("<xmlattr>.Name", name);
		item.put("<xmlattr>.Value", value);
		body.add_child("tt:Source.tt:SimpleItem", item);
	}
	auto add_simple = [&body](const std::string& name, const std::string& value) {
		pt::ptree item;
		item.put("<xmlattr>.Name", name);
		item.put("<xmlattr>.Value", value);
		body.add_child("tt:Data.tt:SimpleItem", item);
	};
	if (!message.data_name.empty())
		add_simple(message.data_name, message.data_value);
	for (const auto& [name, value] : message.data_simple_items)
		add_simple(name, value);
	for (const auto& item : message.data_element_items)
		body.add_child("tt:Data.tt:ElementItem", item);
	node.add_child("wsnt:Message.tt:Message", body);
	return node;
}

std::string serialize_metadata(const std::deque<NotificationMessage>& messages,
															 const std::map<std::string, std::string>& namespaces,
															 const std::vector<scene::Frame>& frames)
{
	auto bindings = namespaces;
	bindings["tt"] = "http://www.onvif.org/ver10/schema";
	bindings["wsnt"] = "http://docs.oasis-open.org/wsn/b-2";
	bindings["tns1"] = "http://www.onvif.org/ver10/topics";
	pt::ptree stream;
	for (const auto& [prefix, uri] : bindings)
		stream.put("<xmlattr>.xmlns:" + prefix, uri);
	for (const auto& frame : frames)
		stream.add_child("tt:VideoAnalytics.tt:Frame", scene::FrameXml(frame));
	for (const auto& message : messages)
		stream.add_child("tt:Event.wsnt:NotificationMessage", serialize_notification(message));
	pt::ptree root;
	root.add_child("tt:MetadataStream", stream);
	std::ostringstream output;
	pt::write_xml(output, root);
	return output.str();
}

std::size_t notification_size(const NotificationMessage& message)
{
	pt::ptree root;
	root.add_child("wsnt:NotificationMessage", serialize_notification(message));
	std::ostringstream output;
	pt::write_xml(output, root);
	return output.str().size() + message.analytics_configuration.size();
}
} // namespace osrv::event
