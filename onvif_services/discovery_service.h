#pragma once

#include <string>

#include <boost/property_tree/ptree.hpp>

class ILogger;

namespace osrv
{
namespace discovery
{
// should be called before before start
void init_service(const std::string& /*configs_path*/, ILogger& /*logger*/);

/**
 * will throw an exception if it's called before @init
 */
void start();

void stop();

namespace utility
{
std::string extract_types(const boost::property_tree::ptree& /*tree*/);

std::string extract_message_id(const boost::property_tree::ptree& /*probe_msg*/);

/**
 * used when a static response message read from a file
 * to change MessageID and RelatesTo values
 */
std::string prepare_response(const std::string& /*messageID*/, const std::string& /*relatesTo*/,
														 std::string&& /*response*/);

/**
 * user should pass corrected uuid
 * there is no checks is complete on correctness of the passed uuid
 */
std::string generate_uuid(std::string uuid = "urn:uuid:1419d68a-1dd2-11b2-a105-000000000000");
} // namespace utility
} // namespace discovery
} // namespace osrv