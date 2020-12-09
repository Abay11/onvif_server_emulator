#pragma once

#include "../Logger.h"
#include "../utility/DateTime.hpp"

#include <queue>
#include <string>
#include <thread>
#include <memory>

#include <boost/asio.hpp>
#include <boost/signals2.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "../Types.inl"
#include "../Simple-Web-Server/server_http.hpp"

namespace osrv
{
	namespace event
	{
		struct NotificationMessage
		{
			std::string topic;
			std::string utc_time;
			std::string property_operation;

			std::string source_name;
			std::string source_value;

			std::string data_name;
			std::string data_value;
		};

		class PullPoint
		{
		public:

			using pull_messages_handler_t = std::function<void(const std::string& subscription_reference,
				std::queue<NotificationMessage>&& events)>;

			PullPoint(const std::string& subscription_reference, boost::asio::io_context& io_context, const ILogger& logger)
				: logger_(&logger)
				, io_context_(io_context)
				, subscription_ref_(subscription_reference)
				, timeout_timer_(io_context)
				, max_messages_(50)
				, is_client_waiting_(false)
			{
				current_time_ = boost::posix_time::microsec_clock::universal_time();
			}

			std::string GetSubscriptionReference() const
			{
				return subscription_ref_;
			}

			// This method is called when a subscriber want to pull events
			void PullMessages(pull_messages_handler_t handler)
			{
				is_client_waiting_ = true;

				if (!events_.empty())
				{
					// Response to a subcriber immediately
					response_to_pullmessages();
				}

				// Do charge the timeout timer
				timeout_timer_.cancel();
				timeout_timer_.expires_after(std::chrono::seconds(timeout_interval_));
				timeout_timer_.async_wait([&handler, this](const boost::system::error_code& error) {
						if (error == boost::asio::error::operation_aborted)
						{
							return;
						}

						response_to_pullmessages();
					});
			}

			// This is method by which event generators should pass events,
			// a new event should be stored to the queue
			void Notify(NotificationMessage&& event)
			{
				events_.push(std::move(event));

				response_to_pullmessages();
			}

			std::string GetLastRenew()
			{
				return utility::datetime::posix_datetime_to_utc(current_time_);
			}

			std::string GetTerminationTime()
			{
				return utility::datetime::posix_datetime_to_utc(current_time_ + boost::posix_time::seconds(timeout_interval_));
			}

			void SetMaxMessages(size_t n)
			{
				max_messages_ = n;
			}

		protected:
			// This is called in 3 cases:
			// 1. when PullMessages requested and the event's queue is not empty (response immediately)
			// 2. when a new event is generated
			// 3. by timeout timer, if there are no events were generated (response with an empty message)
			void response_to_pullmessages()
			{
				if (!is_client_waiting_)
					return;

				logger_->Debug("PullMessages response");
				
				// Do serialize all stored events

				// Do copy only less then specified in a PullMessages messages limit
				// FIX: in current implementation all events is copied
				std::queue<NotificationMessage> copied_events;
				copied_events.swap(events_);
				handler_(subscription_ref_, std::move(copied_events));
				is_client_waiting_ = false;
			}

		private:
			const ILogger* logger_;
			boost::asio::io_context& io_context_;
			boost::asio::steady_timer timeout_timer_;

			boost::posix_time::ptime current_time_;

			const std::string subscription_ref_;
			int timeout_interval_ = 60;

			int max_messages_;

			std::queue<NotificationMessage> events_;

			pull_messages_handler_t handler_;

			bool is_client_waiting_;
		};

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

		protected:
			void generate_event() override;
		};

		// NotificationsManager class links clients, PullPoint instances and event generators.
		// Logic of their cooperation work is implemented in this class.
		class NotificationsManager
		{
		public:
			NotificationsManager(const ILogger& logger)
				:
				logger_(&logger)
			{
			}

			// This method is used to handle corresponding Onvif PullPoint subscription request
			// It's required to generate unique link for each subscriber 
			// Also need to schedule a subscription expiration timeout - and in that case delete subscription
			// Returns the created subscription's reference
			std::shared_ptr<PullPoint> CreatePullPoint()
			{
				// When register a new PullPoint
				// depending on subcription filter in a request
				// need to connect a PullPoint instance only with appropriate event generators
				// FIX: the current implementation connects PullPoint instances with all generators
				auto test_subscription_reference = "onvif/event_service/s0";
				auto pp = std::shared_ptr<PullPoint>(new PullPoint(test_subscription_reference, io_context_, *logger_));
				pullpoints_.push_back(pp);
				for (auto& eg : event_generators_)
				{
					eg->Connect([pp, this](NotificationMessage event_description) {
							pp->Notify(std::move(event_description));
						});
				}

				return pp;
			}

			// If there are messages for specified subscriber - return them immediately
			// Otherwise wait until timeout or any events will be generated 
			void PullMessages(std::shared_ptr<HttpServer::Response> response,
				const std::string& subscription_reference, int timeout, int msg_limit);

			// Delete PullPoint and cancel all related timers
			// Upd.: It seems need to delete all existed PullPoint instances, need to clarify how it's required by the standard
			void Unsubscribe(const std::string& subscription_reference)
			{
			}

			void run()
			{
				for (auto& eg : event_generators_)
				{
					eg->Run();
				}

				io_work_ = std::unique_ptr<work_t>(new work_t(io_context_));
				
				worker_thread_ = std::unique_ptr<std::thread>(new std::thread(
					[this]() {
						io_context_.run();
					}
				));

				logger_->Debug("NotificationsManager is run successfully");
			}

			void add_generator(std::shared_ptr<IEventGenerator> eg)
			{
				event_generators_.push_back(eg);
			}

			boost::asio::io_context& get_io_context()
			{
				return io_context_;
			}

			~NotificationsManager() {}

		private:
			void do_response(const std::string& /*ref*/, std::shared_ptr<HttpServer::Response> /*response*/);

		private:
			const ILogger* logger_;

			boost::asio::io_context io_context_;
			using work_t = boost::asio::io_context::work;
			std::unique_ptr<work_t> io_work_;
			std::unique_ptr<std::thread> worker_thread_;

			// each subcriber have it's PullPoint instance
			std::vector<std::shared_ptr<PullPoint>> pullpoints_;

			std::vector<std::shared_ptr<IEventGenerator>> event_generators_;
		};

		struct PullMessagesRequest
		{
			std::string timeout;
			int messages_limit;

			std::string header_action;
			std::string header_to;
		};

		PullMessagesRequest parse_pullmessages(const std::string&);

		void pullmessages_response_to_soap(PullPoint);

		//return compare references without address, only end
		bool compare_subscription_references(const std::string& /*ref_with_address_prefix*/,
			const std::string& /*reference_path*/);
	}

}