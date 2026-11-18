#pragma once

#include "../HttpServerFwd.h"

#include <boost/asio/io_context.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ILogger;

namespace osrv
{
struct ServerConfigs;

namespace event
{
class IEventGenerator;

// Factories run before the notification loop starts. Generators retain their own dependencies.
using EventGeneratorFactory = std::function<std::shared_ptr<IEventGenerator>(const boost::property_tree::ptree&,
																																						 boost::asio::io_context&, const ILogger&)>;

struct EventTopicDescription
{
	std::string topic;
	std::vector<std::pair<std::string, std::string>> source_properties;
	std::vector<std::pair<std::string, std::string>> data_properties;
};

struct EventGeneratorRegistration
{
	EventGeneratorFactory create;
	EventTopicDescription description;
};

void init_service(HttpServer& /*srv*/, const osrv::ServerConfigs& /*configs*/, const std::string& /*configs_path*/,
									ILogger& /*logger*/, const std::vector<EventGeneratorRegistration>& generators = {});
} // namespace event
} // namespace osrv
