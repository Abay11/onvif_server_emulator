#include "event_generators.h"
#include "pull_point.h"

#include "../utility/DateTime.hpp"


namespace osrv
{
	namespace event
	{

		DInputEventGenerator::DInputEventGenerator(int interval, boost::asio::io_context& io_context, const ILogger& logger_)
			: IEventGenerator(interval, io_context, logger_)
		{
		}

		void DInputEventGenerator::SetDigitalInputsList(const DigitalInputsList& di_list)
		{
			di_list_ = &di_list;
		}

		std::deque<NotificationMessage> DInputEventGenerator::GenerateSynchronizationEvent() const
		{
			TRACE_LOG(logger_);

			if (!di_list_)
				return {};

			std::deque<NotificationMessage> result;
			for (const auto& di : *di_list_)
			{
				NotificationMessage nm;
				nm.topic = "tns1:Device/Trigger/DigitalInput";
				nm.utc_time = utility::datetime::system_utc_datetime();
				nm.property_operation = "Initialized";
				nm.source_name = "InputToken";
				nm.source_value = di->GetToken();
				nm.data_name = "LogicalState";
				nm.data_value = di->GetState() ? "true" : "false";

				result.push_back(nm);
			}

			return result;
		}

		void DInputEventGenerator::generate_event()
		{
			TRACE_LOG(logger_);

			if (!di_list_)
				return;

			for (const auto& di : *di_list_)
			{
				if (!di->IsEnabled())
					continue;

				NotificationMessage nm;
				nm.topic = "tns1:Device/Trigger/DigitalInput";
				nm.utc_time = utility::datetime::system_utc_datetime();
				nm.property_operation = "Changed";
				nm.source_name = "InputToken";
				nm.source_value = di->GetToken();
				nm.data_name = "LogicalState";

				// each time invert state
				nm.data_value = di->InvertState() ? "true" : "false";

				event_signal_(nm);
			}
		}

	}
}