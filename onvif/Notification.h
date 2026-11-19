#pragma once

#include <boost/property_tree/ptree.hpp>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace osrv::event
{
struct NotificationMessage
{
	std::string topic;
	std::string utc_time;
	std::string property_operation;
	std::vector<std::pair<std::string, std::string>> source_item_descriptions;
	std::string data_name;
	std::string data_value;
	std::vector<std::pair<std::string, std::string>> data_simple_items;
	std::vector<boost::property_tree::ptree> data_element_items;
};

using NotificationPtr = std::shared_ptr<const NotificationMessage>;

// Returns the contents of one wsnt:NotificationMessage, without a transport envelope.
boost::property_tree::ptree serialize_notification(const NotificationMessage& message);
std::string serialize_metadata(const std::deque<NotificationMessage>& messages,
															 const std::map<std::string, std::string>& namespaces = {});
std::size_t notification_size(const NotificationMessage& message);
} // namespace osrv::event
