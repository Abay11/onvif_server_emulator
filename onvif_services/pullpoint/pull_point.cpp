#include "pull_point.h"

#include "../utility/XmlParser.h"
#include "../utility/SoapHelper.h"
#include "../utility/HttpHelper.h"
#include "../utility/DateTime.hpp"


#include <sstream>
#include <algorithm>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace osrv
{

	namespace event {

		void PullPoint::PullMessages(pull_messages_handler_t handler, std::shared_ptr<HttpServer::Response> response)
		{
			is_client_waiting_ = true;

			handler_ = handler;
			response_writer_ = response;

			if (!events_.empty())
			{
				// Response to a subcriber immediately
				response_to_pullmessages();
			}

			// Do charge the timeout timer
			timeout_timer_.cancel();
			timeout_timer_.expires_after(std::chrono::seconds(timeout_interval_));
			timeout_timer_.async_wait([handler, this](const boost::system::error_code& error) {
					if (error)
						return;

					response_to_pullmessages();
				});
		}

		void PullPoint::Notify(NotificationMessage&& event)
		{
			events_.push_back(std::move(event));

			response_to_pullmessages();
		}
		
		void PullPoint::response_to_pullmessages()
		{
			if (!is_client_waiting_)
				return;

			// Do serialize all stored events

			// Do copy only less then specified in a PullMessages messages limit
			// FIX: in current implementation all events is copied
			std::deque<NotificationMessage> copied_events;
			copied_events.swap(events_);
			handler_(subscription_ref_, std::move(copied_events), response_writer_);
			response_writer_.reset(); // it's required to reset writer ptr, otherwise response will not be written in time
			is_client_waiting_ = false;
		}
		
		void PullPoint::SetSynchronizationPoint()
		{

			// I think we should clean already saved NotificationMessages
			events_.clear();

			for (const auto eg : connected_generators_)
			{
				auto gen_ev = eg->GenerateSynchronizationEvent();
				events_.insert(events_.end(), gen_ev.begin(), gen_ev.end());
			}
		}
		
		std::shared_ptr<PullPoint> NotificationsManager::CreatePullPoint()
		{
			// When register a new PullPoint
			// depending on subcription filter in a request
			// need to connect a PullPoint instance only with appropriate event generators
			// FIX: the current implementation connects PullPoint instances with all generators

			// FIX: current implementation handles only 1 subscriber, if some pullpoint did not be renewed,
			// it should be deleted by timeout
			auto test_subscription_reference = "onvif/event_service/s0";
			auto pp = std::shared_ptr<PullPoint>(new PullPoint(test_subscription_reference, io_context_, *logger_));
			pullpoints_.push_back(pp);
			for (auto& eg : event_generators_)
			{
				// It's may increase waiting time for already connected clients
				// and now it properly works only for 1 subscriber
				// but it's help to notifiying that one exactly in specified time interval
				eg->Stop();
				eg->Run();

				auto signal_connection = eg->Connect([pp, this](NotificationMessage event_description) {
						pp->Notify(std::move(event_description));
					});
				pp->AddGenerator(eg.get(), signal_connection);
			}

			return pp;
		}
		
		void NotificationsManager::PullMessages(std::shared_ptr<HttpServer::Response> response,
			const std::string& subscription_reference, const std::string& msg_id, int timeout, int msg_limit)
		{
			auto pp_it = find_pullpoint(pullpoints_, subscription_reference);

			if (pp_it != pullpoints_.end())
			{
				(*pp_it)->PullMessages([msg_id, this](const std::string& subscr_ref, std::deque<NotificationMessage> events,
						std::shared_ptr<HttpServer::Response> response) {
						do_pullmessages_response(subscr_ref, msg_id, std::move(events), response);
					}, response);
			}
			else
			{
				// ? Need to check specification, more likely it's need to response with an error code
				logger_->Error("Not found subscription reference: " + subscription_reference);
				return;
			}
		}

		void NotificationsManager::SetSynchronizationPoint(const std::string& subscr_ref)
		{
			auto pp_it = find_pullpoint(pullpoints_, subscr_ref);

			if (pp_it == pullpoints_.end())
			{
				throw std::runtime_error("Invalid subscription reference");
			}

			(*pp_it)->SetSynchronizationPoint();
		}

		void NotificationsManager::Unsubscribe(const std::string& subscription_reference)
		{
			auto pp_it = find_pullpoint(pullpoints_, subscription_reference);
			if (pp_it != pullpoints_.end())
			{
				(*pp_it)->DisconnectFromGenerators();
				pullpoints_.erase(pp_it);
			}
			else
			{
				// TODO: Probably it should be throwed an exception
			}
		}

		void NotificationsManager::Renew(std::shared_ptr<HttpServer::Response> response, const std::string& header_to, const std::string& header_msg_id)
		{
			if (!xml_namespaces_)
				throw std::runtime_error("XML namespaces not initialized in NotificationManager!");
			
			logger_->Debug("Sending RenewRequest: " + header_to);

			namespace pt = boost::property_tree;

			pt::ptree analytics_configs;
			auto envelope_tree = utility::soap::getEnvelopeTree(*xml_namespaces_);

			envelope_tree.add("s:Header.wsa:MessageID", header_msg_id);
			envelope_tree.add("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
			envelope_tree.add("s:Header.wsa:Action", "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewResponse");

			pt::ptree response_node;
			response_node.add("wsnt:TerminationTime",
				utility::datetime::posix_datetime_to_utc(boost::posix_time::microsec_clock::universal_time()
					+ boost::posix_time::seconds(60)));
			response_node.add("wsnt:CurrentTime", utility::datetime::system_utc_datetime());
			envelope_tree.add_child("s:Body.wsnt:RenewResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
		
		void NotificationsManager::Run()
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

		void NotificationsManager::do_pullmessages_response(const std::string& subscr_ref, const std::string& msg_id,
			std::deque<NotificationMessage>&& events, std::shared_ptr<HttpServer::Response> response)
		{
			logger_->Debug("Sending PullPoint response with msg id: " + subscr_ref);

			/**
				PullMessagesResponse response format:
				CurrentTime
				TerminationTime
				NotificationMessage
			*/

			if (!xml_namespaces_)
				throw std::runtime_error("XML namespaces not initialized in NotificationManager!");

			namespace pt = boost::property_tree;

			pt::ptree analytics_configs;
			auto envelope_tree = utility::soap::getEnvelopeTree(*xml_namespaces_);

			envelope_tree.add("s:Header.wsa:MessageID", msg_id);
			envelope_tree.add("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
			envelope_tree.add("s:Header.wsa:Action", "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesResponse");

			pt::ptree response_node = serialize_notification_messages(events, subscr_ref);

			envelope_tree.add_child("s:Body.tet:PullMessagesResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

		bool compare_subscription_references(const std::string& full_ref, const std::string& short_ref)
		{
			// checks if full_ref ends with short_ref, i.e. address prefix with port should be ignored
			// http://127.0.0.1:8080/onvif/event_service/s0
			// onvif/event_service/s0

			return std::find_end(full_ref.begin(), full_ref.end(),
				short_ref.begin(), short_ref.end()) != full_ref.end();
		}

		boost::property_tree::ptree serialize_notification_messages(std::deque<NotificationMessage>& msgs,
			const std::string& subscription_ref)
		{
			namespace pt = boost::property_tree;
			pt::ptree result;

			namespace ptime = boost::posix_time;
			result.add("tet:CurrentTime", utility::datetime::system_utc_datetime());

			auto ttime = ptime::microsec_clock::universal_time();
			ttime += ptime::seconds(60);

			auto t = utility::datetime::posix_datetime_to_utc(ttime);
			result.add("tet:TerminationTime",
				t);

			while(!msgs.empty())
			{
				auto msg = msgs.front();
				msgs.pop_front();

				pt::ptree msg_node;
				// TODO: delete all code related to these parameters, therefore they are not needed is this logic
				//msg_node.add("wsnt:SubscriptionReference.wsa:Address", "http://192.168.43.120:8080/" + subscription_ref);	// <--- these  two are really
				//msg_node.add("wsnt:ProducerReference.wsa:Address", "http://192.168.43.120:8080/onvif/event_service");		// <--- required??? - udp: NOO

				msg_node.add("wsnt:Topic", msg.topic);
				msg_node.add("wsnt:Topic.<xmlattr>.Dialect", "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
				
				msg_node.add("wsnt:Message.tt:Message.<xmlattr>.PropertyOperation", msg.property_operation);
				msg_node.add("wsnt:Message.tt:Message.<xmlattr>.UtcTime", msg.utc_time);

				for (const auto& [name, value] : msg.source_item_descriptions)
				{
					pt::ptree item_descr;
					item_descr.add("<xmlattr>.Name", name);
					item_descr.add("<xmlattr>.Value", value);
					msg_node.add_child("wsnt:Message.tt:Message.tt:Source.tt:SimpleItem", item_descr);
				}
				
				msg_node.add("wsnt:Message.tt:Message.tt:Data.tt:SimpleItem.<xmlattr>.Value", msg.data_value);
				msg_node.add("wsnt:Message.tt:Message.tt:Data.tt:SimpleItem.<xmlattr>.Name", msg.data_name);
								
				result.add_child("wsnt:NotificationMessage", msg_node);
			}

			return result;
		}

		PullPoints_t::const_iterator find_pullpoint(const PullPoints_t& pullpoints, const std::string& subscription_reference)
		{
			return std::find_if(pullpoints.begin(), pullpoints.end(),
				[subscription_reference](PullPoints_t::value_type pp) {

					return compare_subscription_references(subscription_reference,
						pp->GetSubscriptionReference());

				});
		}

	}
}
