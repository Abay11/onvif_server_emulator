#include "pull_point.h"
#include "../../utility/HttpHelper.h"
#include "../../utility/SoapHelper.h"
#include "event_generators.h"

#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <sstream>

namespace osrv::event
{
std::string serialize_subscription_fault(const SubscriptionFault& fault)
{
	boost::property_tree::ptree root;
	auto& envelope = root.put_child("s:Envelope", {});
	envelope.put("<xmlattr>.xmlns:s", "http://www.w3.org/2003/05/soap-envelope");
	envelope.put("<xmlattr>.xmlns:wsnt", "http://docs.oasis-open.org/wsn/b-2");
	envelope.put("<xmlattr>.xmlns:wsrf-r", "http://docs.oasis-open.org/wsrf/r-2");
	envelope.put("<xmlattr>.xmlns:wsrf-bf", "http://docs.oasis-open.org/wsrf/bf-2");
	envelope.put("<xmlattr>.xmlns:wsa", "http://www.w3.org/2005/08/addressing");
	envelope.put("s:Header.wsa:Action", fault.element == "wsrf-r:ResourceUnknownFault"
																					? "http://docs.oasis-open.org/wsrf/fault"
																					: "http://docs.oasis-open.org/wsn/fault");
	envelope.put("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
	envelope.put("s:Body.s:Fault.s:Code.s:Value", "s:Sender");
	envelope.put("s:Body.s:Fault.s:Reason.s:Text", fault.what());
	envelope.put("s:Body.s:Fault.s:Reason.s:Text.<xmlattr>.xml:lang", "en");
	envelope.put("s:Body.s:Fault.s:Detail." + fault.element + ".wsrf-bf:Timestamp",
							 utility::datetime::system_utc_datetime());
	std::ostringstream output;
	boost::property_tree::write_xml(output, root);
	return output.str();
}

PullPoint::PullPoint(std::string reference, std::shared_ptr<EventSource> source, const ILogger& logger)
		: logger_(logger), source_(std::move(source)), subscription_(source_->Subscribe()),
			reference_(std::move(reference)), wait_timer_(source_->GetIoContext()), expiry_timer_(source_->GetIoContext()),
			current_time_(boost::posix_time::microsec_clock::universal_time())
{
}

PullPoint::~PullPoint()
{
	Close();
}

std::string PullPoint::GetSubscriptionReference() const
{
	return reference_;
}
std::string PullPoint::GetLastRenew() const
{
	return source_->Invoke([this]() { return utility::datetime::posix_datetime_to_utc(current_time_); });
}
std::string PullPoint::GetTerminationTime() const
{
	return source_->Invoke([this]() {
		return utility::datetime::posix_datetime_to_utc(current_time_ + boost::posix_time::seconds(lease_.count()));
	});
}

void PullPoint::PullMessages(pull_messages_handler_t handler, std::shared_ptr<HttpServer::Response> response,
														 int timeout, std::size_t limit)
{
	if (closed_)
		throw std::runtime_error("Subscription is closed");
	if (handler_)
		throw SubscriptionFault("wsnt:UnableToGetMessagesFault", "A PullMessages request is already pending");
	if (timeout < 0 || !limit)
		throw std::invalid_argument("PullMessages timeout and message limit are invalid");
	handler_ = std::move(handler);
	response_ = std::move(response);
	message_limit_ = limit;
	auto weak = weak_from_this();
	subscription_->AsyncWait(source_->GetIoContext(), [weak]() {
		if (auto self = weak.lock())
			self->Respond();
	});
	wait_timer_.expires_after(std::chrono::seconds(timeout));
	wait_timer_.async_wait([weak](const boost::system::error_code& error) {
		if (!error)
			if (auto self = weak.lock())
				self->Respond();
	});
}

void PullPoint::Respond()
{
	if (!handler_)
		return;
	if (!closed_ && subscription_->NeedsSynchronization())
		source_->Synchronize(subscription_);
	auto batch = subscription_->Take(message_limit_);
	if (batch.dropped)
		logger_.Warn("PullPoint queue lost " + std::to_string(batch.dropped) + " notifications: " + reference_);
	std::deque<NotificationMessage> messages;
	for (const auto& message : batch.messages)
		messages.push_back(*message);
	subscription_->CancelWait();
	wait_timer_.cancel();
	auto handler = std::move(handler_);
	auto response = std::move(response_);
	handler(reference_, std::move(messages), std::move(response));
}

void PullPoint::SetSynchronizationPoint()
{
	// Append snapshots without erasing pending transient notifications.
	source_->Synchronize(subscription_);
}

void PullPoint::Renew(std::function<void()> expired, std::chrono::seconds lease)
{
	lease_ = lease;
	current_time_ = boost::posix_time::microsec_clock::universal_time();
	expiry_timer_.expires_after(lease);
	expiry_timer_.async_wait([expired = std::move(expired)](const boost::system::error_code& error) {
		if (!error)
			expired();
	});
}

void PullPoint::Close()
{
	if (closed_)
		return;
	closed_ = true;
	subscription_->CancelWait();
	wait_timer_.cancel();
	expiry_timer_.cancel();
	if (handler_)
		Respond();
	subscription_->Close();
}

NotificationsManager::NotificationsManager(const ILogger& logger, const std::map<std::string, std::string>& namespaces,
																					 std::shared_ptr<EventSource> source)
		: logger_(logger), namespaces_(namespaces), source_(std::move(source))
{
}

NotificationsManager::~NotificationsManager()
{
	source_->Invoke([this]() {
		for (const auto& point : pullpoints_)
			point->Close();
		pullpoints_.clear();
	});
}

std::shared_ptr<PullPoint> NotificationsManager::Find(const std::string& reference)
{
	auto found = find_pullpoint(pullpoints_, reference);
	if (found == pullpoints_.end())
		throw SubscriptionFault("wsrf-r:ResourceUnknownFault", "Invalid subscription reference");
	return *found;
}

void NotificationsManager::RenewLease(const std::shared_ptr<PullPoint>& point, int pending_timeout)
{
	point->Renew([this, reference = point->GetSubscriptionReference()]() { Unsubscribe(reference); },
							 std::chrono::seconds(60) + std::chrono::seconds(std::max(0, pending_timeout)));
}

std::shared_ptr<PullPoint> NotificationsManager::CreatePullPoint()
{
	return source_->Invoke([this]() {
		auto reference = "onvif/event_service/s" + std::to_string(next_id_++);
		auto point = std::make_shared<PullPoint>(reference, source_, logger_);
		pullpoints_.push_back(point);
		RenewLease(point);
		return point;
	});
}

void NotificationsManager::PullMessages(std::shared_ptr<HttpServer::Response> response, const std::string& reference,
																				const std::string& message_id, int timeout, int limit)
{
	source_->Invoke([this, response, reference, message_id, timeout, limit]() {
		auto point = Find(reference);
		if (limit <= 0)
			throw std::invalid_argument("MessageLimit must be positive");
		RenewLease(point, timeout);
		point->PullMessages(
				[this, message_id](const std::string& reference, std::deque<NotificationMessage>&& events,
													 std::shared_ptr<HttpServer::Response> response) {
					auto point = Find(reference);
					if (point->IsClosed())
					{
						response->write(SimpleWeb::StatusCode::server_error_internal_server_error,
														serialize_subscription_fault({"wsrf-r:ResourceUnknownFault", "Subscription closed"}),
														{{"Content-Type", "application/soap+xml; charset=utf-8"}});
						return;
					}
					RenewLease(point);
					do_pullmessages_response(reference, message_id, std::move(events), std::move(response));
				},
				response, timeout, static_cast<std::size_t>(limit));
	});
}

void NotificationsManager::SetSynchronizationPoint(const std::string& reference)
{
	source_->Invoke([this, reference]() { Find(reference)->SetSynchronizationPoint(); });
}

void NotificationsManager::Unsubscribe(const std::string& reference)
{
	source_->Invoke([this, reference]() {
		auto point = Find(reference);
		point->Close();
		std::erase(pullpoints_, point);
	});
}

void NotificationsManager::Renew(std::shared_ptr<HttpServer::Response> response, const std::string& reference,
																 const std::string& message_id)
{
	source_->Invoke([this, response, reference, message_id]() {
		auto point = Find(reference);
		RenewLease(point);
		auto envelope = utility::soap::getEnvelopeTree(namespaces_);
		envelope.put("s:Header.wsa:MessageID", message_id);
		envelope.put("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
		envelope.put("s:Header.wsa:Action", "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewResponse");
		envelope.put("s:Body.wsnt:RenewResponse.wsnt:TerminationTime", point->GetTerminationTime());
		envelope.put("s:Body.wsnt:RenewResponse.wsnt:CurrentTime", point->GetLastRenew());
		boost::property_tree::ptree root;
		root.add_child("s:Envelope", envelope);
		std::ostringstream output;
		boost::property_tree::write_xml(output, root);
		utility::http::fillResponseWithHeaders(*response, output.str());
	});
}

void NotificationsManager::Run()
{
	source_->Run();
}
void NotificationsManager::AddGenerator(std::shared_ptr<IEventGenerator> generator)
{
	source_->AddGenerator(std::move(generator));
}
boost::asio::io_context& NotificationsManager::GetIoContext()
{
	return source_->GetIoContext();
}

void NotificationsManager::do_pullmessages_response(const std::string& subscr_ref, const std::string& msg_id,
																										std::deque<NotificationMessage>&& events,
																										std::shared_ptr<HttpServer::Response> response)
{
	logger_.Debug("Sending PullPoint response with msg id: " + subscr_ref);

	/**
		PullMessagesResponse response format:
		CurrentTime
		TerminationTime
		NotificationMessage
	*/

	namespace pt = boost::property_tree;

	pt::ptree analytics_configs;
	auto envelope_tree = utility::soap::getEnvelopeTree(namespaces_);

	envelope_tree.add("s:Header.wsa:MessageID", msg_id);
	envelope_tree.add("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
	envelope_tree.add("s:Header.wsa:Action",
										"http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesResponse");

	pt::ptree response_node = serialize_notification_messages(events, subscr_ref);
	response_node.put("tet:TerminationTime", Find(subscr_ref)->GetTerminationTime());

	envelope_tree.add_child("s:Body.tet:PullMessagesResponse", response_node);

	pt::ptree root_tree;
	root_tree.put_child("s:Envelope", envelope_tree);

	std::ostringstream os;
	pt::write_xml(os, root_tree);

	utility::http::fillResponseWithHeaders(*response, os.str());
}

bool compare_subscription_references(const std::string& full, const std::string& reference)
{
	if (full == reference || full == "/" + reference)
		return true;
	auto scheme = full.find("://");
	if (scheme == std::string::npos)
		return false;
	auto path = full.find('/', scheme + 3);
	return path != std::string::npos && full.substr(path) == "/" + reference;
}

boost::property_tree::ptree serialize_notification_messages(const std::deque<NotificationMessage>& messages,
																														const std::string& subscription_reference)
{
	boost::property_tree::ptree result;
	result.put("tet:CurrentTime", utility::datetime::system_utc_datetime());
	result.put("tet:TerminationTime",
						 utility::datetime::posix_datetime_to_utc(boost::posix_time::microsec_clock::universal_time() +
																											boost::posix_time::seconds(60)));
	for (const auto& message : messages)
		result.add_child("wsnt:NotificationMessage", serialize_notification(message));
	return result;
}

PullPoints_t::const_iterator find_pullpoint(const PullPoints_t& points, const std::string& reference)
{
	return std::find_if(points.begin(), points.end(), [&reference](const auto& point) {
		return compare_subscription_references(reference, point->GetSubscriptionReference());
	});
}
} // namespace osrv::event
