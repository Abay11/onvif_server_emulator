#include "event_generators.h"
#include "pull_point.h"

#include "../utility/DateTime.hpp"


namespace osrv
{
	namespace event
	{

		DInputEventGenerator::DInputEventGenerator(int interval, const std::string& topic, boost::asio::io_context& io_context, const ILogger& logger_)
			: IEventGenerator(interval, topic, io_context, logger_)
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
				nm.topic = notifications_topic_;
				nm.utc_time = utility::datetime::system_utc_datetime();
				nm.property_operation = "Initialized";
				nm.source_item_descriptions.push_back({"InputToken", di->GetToken()});
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
				nm.topic = notifications_topic_;
				nm.utc_time = utility::datetime::system_utc_datetime();
				nm.property_operation = "Changed";
				nm.source_item_descriptions.push_back({"InputToken", di->GetToken()});
				nm.data_name = "LogicalState";

				// each time invert state
				nm.data_value = di->InvertState() ? "true" : "false";

				event_signal_(nm);
			}
		}

		MotionAlarmEventGenerator::MotionAlarmEventGenerator(const std::string& source_token,
			int interval, const std::string& topic,
			boost::asio::io_context& io_context, const ILogger& logger_)
			: IEventGenerator(interval, topic, io_context, logger_),
			source_token_(source_token)
		{
		}

		std::deque<NotificationMessage> MotionAlarmEventGenerator::GenerateSynchronizationEvent() const
		{
			TRACE_LOG(logger_);

			NotificationMessage nm;
			nm.topic = notifications_topic_;
			nm.utc_time = utility::datetime::system_utc_datetime();
			nm.property_operation = "Initialized";
			nm.source_item_descriptions.push_back({"Source", source_token_});
			nm.data_name = "State";
			nm.data_value = "false";

			return { nm };
		}

		void MotionAlarmEventGenerator::generate_event()
		{
			TRACE_LOG(logger_);

			NotificationMessage nm;
			nm.topic = notifications_topic_;
			nm.utc_time = utility::datetime::system_utc_datetime();
			nm.property_operation = "Changed";
			nm.source_item_descriptions.push_back({"Source", source_token_});
			nm.data_name = "State";
			// each time invert state
			nm.data_value = InvertState() ? "true" : "false";

			event_signal_(nm);
		}

		CellMotionEventGenerator::CellMotionEventGenerator(const std::string& vsc_token, const std::string& vac_token,
			const std::string& rule,
			const std::string& din,
			int interval, const std::string& topic, boost::asio::io_context& io_context, const ILogger& logger_)
			: IEventGenerator(interval, topic, io_context, logger_)
			,video_source_configuration_token_(vsc_token)
			,video_analytics_configuration_token_(vac_token)
			,rule_(rule)
			,data_item_name_(din)
		{
		}

		std::deque<NotificationMessage> CellMotionEventGenerator::GenerateSynchronizationEvent() const
		{
			TRACE_LOG(logger_);

			NotificationMessage nm;
			nm.topic = notifications_topic_;
			nm.utc_time = utility::datetime::system_utc_datetime();
			nm.property_operation = "Initialized";
			nm.source_item_descriptions.push_back({"VideoSourceConfigurationToken", video_source_configuration_token_});
			nm.source_item_descriptions.push_back({"VideoAnalyticsConfigurationToken", video_analytics_configuration_token_});
			nm.source_item_descriptions.push_back({"Rule", rule_});
			nm.data_name = data_item_name_;
			nm.data_value = "false";

			return { nm };
		}

		void CellMotionEventGenerator::generate_event()
		{
			TRACE_LOG(logger_);

			NotificationMessage nm;
			nm.topic = notifications_topic_;
			nm.utc_time = utility::datetime::system_utc_datetime();
			nm.property_operation = "Changed";
			nm.source_item_descriptions.push_back({"VideoSourceConfigurationToken", video_source_configuration_token_});
			nm.source_item_descriptions.push_back({"VideoAnalyticsConfigurationToken", video_analytics_configuration_token_});
			nm.source_item_descriptions.push_back({"Rule", rule_});
			nm.data_name = data_item_name_;
			nm.data_value = "false";
			// each time invert state
			nm.data_value = InvertState() ? "true" : "false";

			event_signal_(nm);
		}

		AudioDetectectionEventGenerator::AudioDetectectionEventGenerator(const std::string& sct,
			const std::string& acf, const std::string& r, const std::string& din,
			int timeout, const std::string& topic, boost::asio::io_context& ioc, const ILogger& logger)
			: IEventGenerator(timeout, topic, ioc, logger)
			, source_configuration_token_(sct)
			, analytics_configuration_token_(acf)
			, rule_(r)
			, data_item_name_(din)
		{
		}

		std::deque<NotificationMessage> AudioDetectectionEventGenerator::GenerateSynchronizationEvent() const
		{
			TRACE_LOG(logger_);

			NotificationMessage nm;
			nm.topic = notifications_topic_;
			nm.utc_time = utility::datetime::system_utc_datetime();
			nm.property_operation = "Initialized";
			nm.source_item_descriptions.push_back({"AudioSourceConfigurationToken", source_configuration_token_});
			nm.source_item_descriptions.push_back({"AudioAnalyticsConfigurationToken", 
				analytics_configuration_token_});
			nm.source_item_descriptions.push_back({"Rule", rule_});
			nm.data_name = data_item_name_;
			nm.data_value = "false";

			return { nm };
		}

		void AudioDetectectionEventGenerator::generate_event()
		{
			TRACE_LOG(logger_);

			NotificationMessage nm;
			nm.topic = notifications_topic_;
			nm.utc_time = utility::datetime::system_utc_datetime();
			nm.property_operation = "Changed";
			nm.source_item_descriptions.push_back({"AudioSourceConfigurationToken",
				source_configuration_token_});
			nm.source_item_descriptions.push_back({"AudioAnalyticsConfigurationToken",
				analytics_configuration_token_});
			nm.source_item_descriptions.push_back({"Rule", rule_});
			nm.data_name = data_item_name_;
			// each time invert state
			nm.data_value = InvertState() ? "true" : "false";

			event_signal_(nm);
		}
}
}