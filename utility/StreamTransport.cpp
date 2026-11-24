#include "StreamTransport.h"
#include "../Server.h"
#include "MetadataConfiguration.h"
#include "XmlParser.h"

namespace osrv
{
namespace
{
const boost::property_tree::ptree* Child(const boost::property_tree::ptree& tree, const std::string& name)
{
	const boost::property_tree::ptree* result = nullptr;
	for (const auto& [key, value] : tree)
		if (exns::get_element_without_ns(key) == name)
		{
			if (result)
				throw metadata::Fault("InvalidStreamSetup", "Duplicate stream transport parameter");
			result = &value;
		}
	return result;
}
std::string Text(const boost::property_tree::ptree& tree, const std::string& name)
{
	auto node = Child(tree, name);
	return node ? node->data() : "";
}
} // namespace
const boost::property_tree::ptree& StreamOperation(const boost::property_tree::ptree& document,
																									 const std::string& operation)
{
	auto* node = &document;
	for (const auto& level : {std::string("Envelope"), std::string("Body"), operation})
	{
		node = Child(*node, level);
		if (!node)
			throw metadata::Fault("InvalidStreamSetup", "Missing stream request");
	}
	return *node;
}
std::string StreamAddress(const ServerConfigs& server, const boost::property_tree::ptree& operation, bool media2,
													bool secure)
{
	std::string protocol;
	if (media2)
		protocol = Text(operation, "Protocol");
	else
	{
		auto setup = Child(operation, "StreamSetup");
		auto transport = setup ? Child(*setup, "Transport") : nullptr;
		if (!setup || Text(*setup, "Stream") != "RTP-Unicast" || !transport || Child(*transport, "Tunnel"))
			throw metadata::Fault("InvalidStreamSetup", "Unsupported or missing unicast stream setup");
		protocol = Text(*transport, "Protocol");
	}
	if (protocol == (media2 ? "RtspOverHttp" : "HTTP"))
	{
		if (secure ? !server.https_enabled_ : !server.http_enabled_)
			throw metadata::Fault("InvalidStreamSetup", "Requested web transport is disabled");
		return secure ? server.HttpsAddress() : server.PlainHttpAddress();
	}
	if (media2 ? (protocol == "RTSP" || protocol == "RtspUnicast")
						 : (protocol == "RTSP" || protocol == "TCP" || protocol == "UDP"))
		return server.RtspAddress();
	throw metadata::Fault("InvalidStreamSetup", "Unsupported stream transport protocol");
}
} // namespace osrv
