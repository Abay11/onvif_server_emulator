#include "pull_point.h"

#include "../utility/XmlParser.h"
#include "../utility/SoapHelper.h"
#include "../utility/HttpHelper.h"
#include "../utility/DateTime.hpp"


#include <sstream>
#include <algorithm>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace osrv
{

	namespace event {

		void NotificationsManager::PullMessages(std::shared_ptr<HttpServer::Response> response,
			const std::string& subscription_reference, const std::string& msg_id, int timeout, int msg_limit)
		{
			auto pp_it = std::find_if(pullpoints_.begin(), pullpoints_.end(),
				[subscription_reference](std::shared_ptr<PullPoint> pp) {

					return compare_subscription_references(subscription_reference,
						pp->GetSubscriptionReference());

				});

			if (pp_it != pullpoints_.end())
			{
				(*pp_it)->PullMessages([msg_id, this](const std::string& subscr_ref, std::queue<NotificationMessage> events,
						std::shared_ptr<HttpServer::Response> response) {
						do_pullmessages_response(subscr_ref, msg_id, std::move(events), response);
					}, response);
			}
			else
			{
				// ? Need to check specification, more likely it's need to response with an error code
				logger_->Error("Not found subscription reference: " + subscription_reference);
				return;
			}
		}

		void NotificationsManager::do_pullmessages_response(const std::string& subscr_ref, const std::string& msg_id,
			std::queue<NotificationMessage>&& events, std::shared_ptr<HttpServer::Response> response)
		{
			logger_->Debug("Sending PullPoint response with msg id: " + subscr_ref);

			/**
				PullMessagesResponse response format:
				CurrentTime
				TerminationTime
				NotificationMessage
			*/

			if (!xml_namespaces_)
				throw std::runtime_error("XML namespaces not initialized in NotificationManager!");

			namespace pt = boost::property_tree;

			pt::ptree analytics_configs;
			auto envelope_tree = utility::soap::getEnvelopeTree(*xml_namespaces_);

			envelope_tree.add("s:Header.wsa:MessageID", msg_id);
			envelope_tree.add("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
			envelope_tree.add("s:Header.wsa:Action", "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesResponse");

			pt::ptree response_node = serialize_notification_messages(events);

			envelope_tree.add_child("s:Body.tet:PullMessagesResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

		PullMessagesRequest parse_pullmessages(const std::string& request)
		{
			PullMessagesRequest result;

			auto copy = request;
			std::istringstream is(copy);

			namespace pt = boost::property_tree;
			pt::ptree xml_tree;
			pt::xml_parser::read_xml(is, xml_tree);

			result.header_action = exns::find_hierarchy("Envelope.Header.Action", xml_tree);
			result.msg_id = exns::find_hierarchy("Envelope.Header.MessageID", xml_tree);
			result.header_to = exns::find_hierarchy("Envelope.Header.To", xml_tree);
			result.timeout = exns::find_hierarchy("Envelope.Body.PullMessages.Timeout", xml_tree);
			result.messages_limit = std::stoi((exns::find_hierarchy("Envelope.Body.PullMessages.MessageLimit", xml_tree)));

			return result;
		}

		bool compare_subscription_references(const std::string& full_ref, const std::string& short_ref)
		{
			// checks if full_ref ends with short_ref, i.e. address prefix with port should be ignored
			// http://127.0.0.1:8080/onvif/event_service/s0
			// onvif/event_service/s0

			return std::find_end(full_ref.begin(), full_ref.end(),
				short_ref.begin(), short_ref.end()) != full_ref.end();
		}

		boost::property_tree::ptree serialize_notification_messages(std::queue<NotificationMessage>& msgs)
		{
			namespace pt = boost::property_tree;
			pt::ptree result;

			while(!msgs.empty())
			{
				auto msg = msgs.front();
				msgs.pop();
			}

			namespace ptime = boost::posix_time;
			result.add("tet:CurrentTime", utility::datetime::system_utc_datetime());

			auto ttime = ptime::microsec_clock::universal_time();
			ttime += ptime::seconds(60);

			auto t = utility::datetime::posix_datetime_to_utc(ttime);
			result.add("tet:TerminationTime",
				t);

			return result;
		}

	}
}
