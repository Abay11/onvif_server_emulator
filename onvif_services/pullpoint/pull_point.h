#pragma once

#include "../Logger.h"
#include "../utility/DateTime.hpp"
#include "event_generators.h"

#include <deque>
#include <string>
#include <thread>
#include <memory>

#include <boost/asio.hpp>
#include <boost/signals2.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

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
				std::deque<NotificationMessage>&& events,
				std::shared_ptr<HttpServer::Response>)>;

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

			// Link a connected generator and set a related connection
			// NOTE: if this action is not done during initialization,
			// SetSynchronizationPoint() will return empty list
			void AddGenerator(const IEventGenerator* eg, boost::signals2::connection signal_connection)
			{
				if (eg)
				{
					connected_generators_.push_back(eg);
					signal_connections_.push_back(signal_connection);
				}
			}

			void DisconnectFromGenerators()
			{
				for (const auto& s : signal_connections_)
				{
					s.disconnect();
				}
			}

			std::string GetSubscriptionReference() const
			{
				return subscription_ref_;
			}

			// This method is called when a subscriber want to pull events
			void PullMessages(pull_messages_handler_t handler, std::shared_ptr<HttpServer::Response> response);

			// This is method by which event generators should pass events,
			// a new event should be stored to the queue
			void Notify(NotificationMessage&& event);

			void SetSynchronizationPoint();

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
			void response_to_pullmessages();

		private:
			const ILogger* logger_;
			boost::asio::io_context& io_context_;
			boost::asio::steady_timer timeout_timer_;

			boost::posix_time::ptime current_time_;

			const std::string subscription_ref_;
			int timeout_interval_ = 60;

			int max_messages_;

			std::deque<NotificationMessage> events_;

			pull_messages_handler_t handler_;
			std::shared_ptr<HttpServer::Response> response_writer_;

			bool is_client_waiting_;

			// supposed to used only to get SynchronizationPoint
			std::vector<const IEventGenerator*> connected_generators_;
			std::vector<boost::signals2::connection> signal_connections_;
		};
		using PullPoints_t = std::vector<std::shared_ptr<PullPoint>>;

		// NotificationsManager class links clients, PullPoint instances and event generators.
		// Logic of their cooperation work is implemented in this class.
		class NotificationsManager
		{
		public:
			NotificationsManager(const ILogger& logger, const osrv::StringsMap& xml_namespaces)
				: logger_(&logger)
			{
				// XML namespaces are those, which added in the beginning of responses
				xml_namespaces_ = &xml_namespaces;
			}

			// This method is used to handle corresponding Onvif PullPoint subscription request
			// It's required to generate unique link for each subscriber 
			// Also need to schedule a subscription expiration timeout - and in that case delete subscription
			// Returns the created subscription's reference
			std::shared_ptr<PullPoint> CreatePullPoint();

			// If there are messages for specified subscriber - return them immediately
			// Otherwise wait until timeout or any events will be generated 
			void PullMessages(std::shared_ptr<HttpServer::Response> /*response*/,
				const std::string& /*subscription_reference*/, const std::string& /*msg_id*/, int /*timeout*/, int /*msg_limit*/);

			void SetSynchronizationPoint(const std::string& /*subscr_ref*/);

			// Delete PullPoint and cancel all related timers
			void Unsubscribe(const std::string& /*subscription_reference*/);

			void Renew(std::shared_ptr<HttpServer::Response> /*response*/,
				const std::string& /*header_to*/,
				const std::string& /*header_msg_id*/);

			void Run();

			void AddGenerator(std::shared_ptr<IEventGenerator> eg)
			{
				event_generators_.push_back(eg);
			}

			boost::asio::io_context& GetIoContext()
			{
				return io_context_;
			}

			~NotificationsManager() {}

		private:
			void do_pullmessages_response(const std::string& /*ref*/, const std::string& /*msg_id*/,
				std::deque<NotificationMessage>&& /*events*/, std::shared_ptr<HttpServer::Response> /*response*/);

		private:
			const ILogger* logger_;

			boost::asio::io_context io_context_;
			using work_t = boost::asio::io_context::work;
			std::unique_ptr<work_t> io_work_;
			std::unique_ptr<std::thread> worker_thread_;

			// each subcriber have it's PullPoint instance
			std::vector<std::shared_ptr<PullPoint>> pullpoints_;

			std::vector<std::shared_ptr<IEventGenerator>> event_generators_;

			const osrv::StringsMap* xml_namespaces_ = nullptr;
		};

		struct PullMessagesRequest
		{
			std::string timeout;
			int messages_limit;

			std::string header_action;
			std::string header_to;
			std::string msg_id;
		};

		void pullmessages_response_to_soap(PullPoint);

		//return compare references without address, only end
		bool compare_subscription_references(const std::string& /*ref_with_address_prefix*/,
			const std::string& /*reference_path*/);

		boost::property_tree::ptree serialize_notification_messages(std::deque<NotificationMessage>& /*messages*/,
			const std::string& /*subscription_ref*/);

		PullPoints_t::const_iterator find_pullpoint(const PullPoints_t& /*pullpoints*/,
			const std::string& /*subscription_reference*/);
	}

}