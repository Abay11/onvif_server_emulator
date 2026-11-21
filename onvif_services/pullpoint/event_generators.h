#pragma once

#include "../Logger.h"
#include "../onvif_services/physical_components/IDigitalInput.h"
#include "../../onvif/Notification.h"

#include <functional>
#include <cstdint>
#include <stdexcept>
#include <deque>

#include <boost/signals2.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace utility { class AnalyticsRules; }

namespace osrv
{
	namespace event
	{
		struct NotificationMessage;

		class IEventGenerator
		{
		public:
			IEventGenerator(int interval, const std::string& topic, boost::asio::io_context& io_context,
											const ILogger& logger)
					: event_interval_(interval), notifications_topic_(topic), io_context_(io_context), alarm_timer_(io_context_),
						logger_(logger)
			{
			}

			virtual ~IEventGenerator()
			{
			}

			// Accessed only on the EventSource worker, including manual generation.
			void Run()
			{
				if (running_)
					return;
				running_ = true;
				if (automatic_)
					schedule_next_alarm();
			}
			void Stop()
			{
				running_ = false;
				++generation_;
				alarm_timer_.cancel();
			}
			void Configure(int interval, bool automatic)
			{
				if (interval < 1)
					throw std::invalid_argument("Invalid event interval");
				event_interval_ = interval;
				automatic_ = automatic;
				++generation_;
				alarm_timer_.cancel();
				if (running_ && automatic_)
					schedule_next_alarm();
			}
			void GenerateOnce()
			{
				generate_event();
			}
			const std::string& Topic() const
			{
				return notifications_topic_;
			}

			// This will be connected
			boost::signals2::connection Connect(std::function<void(NotificationMessage)> f)
			{
				return event_signal_.connect(f);
			}

			// returns a NotificationMessage with 'PropertyOperation' equals "Initialized"
			virtual std::deque<NotificationMessage> GenerateSynchronizationEvent() const = 0;

		protected:
			// This method is should be overrided by implementors.
			// Implementors should fill a NotificationMessage and emit the signal with it
			virtual void generate_event()
			{
				//	NotificationMessage event_description;
				// /*do somehow filling event_description...*/
				// event_signal_(NotificationMessage);
			}

			void schedule_next_alarm()
			{
				alarm_timer_.expires_after(std::chrono::seconds(event_interval_));
				const auto generation = generation_;
				alarm_timer_.async_wait([this, generation](const boost::system::error_code& error) {
					if (error || !running_ || !automatic_ || generation != generation_)
						return;
					generate_event();
					if (running_ && automatic_ && generation == generation_)
						schedule_next_alarm();
				});
			}

		protected:
			boost::signals2::signal<void(NotificationMessage)> event_signal_;

		protected:
			int event_interval_;
			bool running_ = false;
			bool automatic_ = true;
			std::uint64_t generation_ = 0;
			const std::string notifications_topic_;

			boost::asio::io_context& io_context_;
			boost::asio::steady_timer alarm_timer_;

			const ILogger& logger_;
		};

		class FaceRecognitionEventGenerator : public IEventGenerator
		{
		public:
			FaceRecognitionEventGenerator(std::shared_ptr<utility::AnalyticsRules> detector,
				boost::asio::io_context& io, const ILogger& logger);
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;
		protected:
			void generate_event() override;
		private:
			std::shared_ptr<utility::AnalyticsRules> detector_;
			size_t step_ = 0;
		};

		class LicensePlateEventGenerator : public IEventGenerator
		{
		public:
			LicensePlateEventGenerator(std::shared_ptr<utility::AnalyticsRules> detector,
				boost::asio::io_context& io, const ILogger& logger);
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;
		protected:
			void generate_event() override;
		private:
			std::shared_ptr<utility::AnalyticsRules> detector_;
			size_t step_ = 0;
		};

		class MotionRegionEventGenerator : public IEventGenerator
		{
		public:
			MotionRegionEventGenerator(std::shared_ptr<utility::AnalyticsRules> detector, int interval,
				boost::asio::io_context& io, const ILogger& logger);
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;
		protected:
			void generate_event() override;
		private:
			std::shared_ptr<utility::AnalyticsRules> detector_;
			unsigned step_ = 0;
		};

		class LineDetectorEventGenerator : public IEventGenerator
		{
		public:
			LineDetectorEventGenerator(std::shared_ptr<utility::AnalyticsRules> detector, int interval,
				boost::asio::io_context& io, const ILogger& logger);
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;
		protected:
			void generate_event() override;
		private:
			std::shared_ptr<utility::AnalyticsRules> detector_;
			unsigned step_ = 0;
		};

		class LineCountingEventGenerator : public IEventGenerator
		{
		public:
			LineCountingEventGenerator(std::shared_ptr<utility::AnalyticsRules> detector,
				boost::asio::io_context& io, const ILogger& logger);
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;

		protected:
			void generate_event() override;

		private:
			std::shared_ptr<utility::AnalyticsRules> detector_;
		};
		class FieldDetectorEventGenerator : public IEventGenerator
		{
		public:
			FieldDetectorEventGenerator(std::shared_ptr<utility::AnalyticsRules> detector, boost::asio::io_context& io,
																	const ILogger& logger);
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;
		protected:
			void generate_event() override;
		private:
			std::shared_ptr<utility::AnalyticsRules> detector_;
		};

		class DInputEventGenerator : public IEventGenerator
		{
		public:
			DInputEventGenerator(int /*interval*/, const std::string& /*topic*/,
				boost::asio::io_context& /*io_context*/, const ILogger& /*logger_*/);

			// If the member DigitalInputsList is not initialized, events will not be generated
			void SetDigitalInputsList(const DigitalInputsList& /*di_list*/);

			// Inherited via IEventGenerator
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;

		protected:
			void generate_event() override;

		private:
			bool state = false;
			const DigitalInputsList* di_list_ = nullptr;
		};
		
		class MotionAlarmEventGenerator : public IEventGenerator
		{
		public:
			MotionAlarmEventGenerator(const std::string& /*source_token*/, int /*interval*/, const std::string& /*topic*/,
				boost::asio::io_context& /*io_context*/, const ILogger& /*logger_*/);

			// Inherited via IEventGenerator
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;

		protected:
			void generate_event() override;

		private:
			bool InvertState()
			{
				return state_ = !state_;
			}

		private:
			bool state_ = false;
			std::string source_token_;
		};

		class CellMotionEventGenerator : public IEventGenerator
		{
		public:
			CellMotionEventGenerator(const std::string& /*vsc_token*/, const std::string& /*vac_token*/,
				const std::string& /*rule*/,
				const std::string& /*data_item_name*/,
				int /*interval*/, const std::string& /*topic*/,
				boost::asio::io_context& /*io_context*/, const ILogger& /*logger_*/);

			// Inherited via IEventGenerator
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;

		protected:
			void generate_event() override;

		private:
			bool InvertState()
			{
				return state_ = !state_;
			}

		private:
			bool state_ = false;
			std::string video_source_configuration_token_;
			std::string video_analytics_configuration_token_;
			std::string rule_;
			std::string data_item_name_;
		};

		class AudioDetectectionEventGenerator : public IEventGenerator
		{
		public:
			AudioDetectectionEventGenerator(const std::string& /*asc_token*/, const std::string& /*ac_token*/,
				const std::string& /*rule*/,
				const std::string& /*data_item_name*/,
				int /*interval*/, const std::string& /*topic*/,
				boost::asio::io_context& /*io_context*/, const ILogger& /*logger_*/);

			// Inherited via IEventGenerator
			std::deque<NotificationMessage> GenerateSynchronizationEvent() const override;

		protected:
			void generate_event() override;

		private:
			bool InvertState()
			{
				return state_ = !state_;
			}

		private:
			bool state_ = false;
			std::string source_configuration_token_;
			std::string analytics_configuration_token_;
			std::string rule_;
			std::string data_item_name_;
		};
	}
}
