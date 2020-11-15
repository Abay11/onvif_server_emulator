#include "pull_point.h"

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



	}
}