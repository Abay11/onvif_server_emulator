#include "event_generators.h"
#include "pull_point.h"

#include "../utility/DateTime.hpp"


namespace osrv
{
	namespace event
	{

		DInputEventGenerator::DInputEventGenerator(int interval, boost::asio::io_context& io_context)
			: IEventGenerator(interval, io_context)
		{
		}

		void DInputEventGenerator::generate_event()
		{
			NotificationMessage nm;
			nm.topic = "tns1:Device/Trigger/DigitalInput";
			nm.utc_time = utility::datetime::system_utc_datetime();
			nm.property_operation = "Changed";
			nm.source_name = "InputToken";
			nm.source_value = "DIGIT_INPUT_0";
			nm.data_name = "LogicalState";

			// each time invert state
			state != state;
			nm.data_value = state ? "true" : "false";

			event_signal_(nm);
		}

	}
}