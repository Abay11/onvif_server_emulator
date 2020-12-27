#pragma once

#include "../onvif_services/physical_components/IDigitalInput.h"

#include <functional>

#include <boost/signals2.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace osrv
{
	namespace event
	{
		struct NotificationMessage;

		class IEventGenerator
		{
		public:
			IEventGenerator(int interval, boost::asio::io_context& io_context)
				:
				event_interval_(interval)
				,io_context_(io_context)
				,alarm_timer_(io_context_)
			{
			}

			virtual ~IEventGenerator() {}

			//Before calling this method, no any events should be generated
			void Run()
			{
				schedule_next_alarm();
			}

			void Stop()
			{
				alarm_timer_.cancel();
			}

			//This will be connected
			void Connect(std::function<void(NotificationMessage)> f)
			{
				event_signal_.connect(f);
			}

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
				alarm_timer_.async_wait([this](const boost::system::error_code& error) {
							if(error == boost::asio::error::operation_aborted)
								return;

							generate_event();

							schedule_next_alarm();
						}
					);
			}

		protected:
			boost::signals2::signal<void(NotificationMessage)> event_signal_;

		protected:
			const int event_interval_;

			boost::asio::io_context& io_context_;
			boost::asio::steady_timer alarm_timer_;
		};

		class DInputEventGenerator : public IEventGenerator
		{
		public:
			DInputEventGenerator(int /*interval*/, boost::asio::io_context& /*io_context*/);

			// If the member DigitalInputsList is not initialized, events will not be generated
			void SetDigitalInputsList(const DigitalInputsList& /*di_list*/);

		protected:
			void generate_event() override;

		private:
			bool state = false;
			const DigitalInputsList* di_list_ = nullptr;

		};
	}
}