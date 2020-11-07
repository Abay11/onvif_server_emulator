#include "pull_point.h"

#include "../utility/XmlParser.h"

#include <sstream>

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

	}
}
