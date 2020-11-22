#include "pull_point.h"

#include "../utility/XmlParser.h"

#include <sstream>
#include <algorithm>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace osrv
{

	namespace event {


		DInputEventGenerator::DInputEventGenerator(int interval, boost::asio::io_context& io_context)
			: IEventGenerator(interval, io_context)
		{
		}

		void DInputEventGenerator::generate_event()
		{
			NotificationMessage nm;
			nm.topic = "tns1:Device/Trigger/DigitalInput";
			nm.utc_time = "2020-10-27T11:10:42Z";
			nm.property_operation = "Initialized";
			nm.source_name = "InputToken";
			nm.source_value = "DIGIT_INPUT_000";
			nm.data_name = "LogicalState";
			nm.data_name = "true";

			event_signal_(nm);
		}

		void NotificationsManager::PullMessages(std::shared_ptr<HttpServer::Response> response,
			const std::string& subscription_reference, int timeout, int msg_limit)
		{
			auto pp_it = std::find_if(pullpoints_.begin(), pullpoints_.end(),
				[subscription_reference](std::shared_ptr<PullPoint> pp) {

					return compare_subscription_references(subscription_reference,
						pp->GetSubscriptionReference());

				});

			if (pp_it != pullpoints_.end())
			{
				(*pp_it)->PullMessages([response, this](const std::string& subscr_ref, std::queue<NotificationMessage> events) {
						do_response(subscr_ref, response);
					});
			}
			else
			{
				// ? Need to check specification, more likely it's need to response with an error code
				logger_->Error("Not found subscription reference: " + subscription_reference);
				return;
			}
		}

		void NotificationsManager::do_response(const std::string& ref, std::shared_ptr<HttpServer::Response> response)
		{
			logger_->Debug("Sending PullPoint response to: " + ref);

			*response << "HTTP/1.1 200 OK\r\n" << "Content-Length: 0\r\n" << "Connection: close\r\n\r\n";
			
			response.reset();
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

	}
}
