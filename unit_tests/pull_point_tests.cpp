#include <boost/test/unit_test.hpp>

#include "../utility/DateTime.hpp"
#include "../utility/XmlParser.h"
#include "../onvif_services/pullpoint/pull_point.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace
{
namespace pt = boost::property_tree;
}

/* FIX: the tested function @parse_pullmessages  was deleted, add restore these tests and generalize function
BOOST_AUTO_TEST_CASE(parse_pullmessages_func_0)
{
	const std::string request =
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<SOAP-ENV:Envelope"
				" xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
				" xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
				" xmlns:tet=\"http://www.onvif.org/ver10/events/wsdl\">"
				"<SOAP-ENV:Header>"
					"<wsa:Action>"
						"http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest"
					"</wsa:Action>"
					"<wsa:To>http://160.10.64.10/Subscription?Idx=0</wsa:To>"
				"</SOAP-ENV:Header>"
			"<SOAP-ENV:Body>"
				"<tet:PullMessages>"
					"<tet:Timeout>"
						"PT5S"
					"</tet:Timeout>"
					"<tet:MessageLimit>"
						"2"
					"</tet:MessageLimit>"
				"</tet:PullMessages>"
			"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>";

	osrv::event::PullMessagesRequest expected;
	expected.header_action = "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest";
	expected.header_to = "http://160.10.64.10/Subscription?Idx=0";
	expected.timeout = "PT5S";
	expected.messages_limit = 2;

	auto actual = osrv::event::parse_pullmessages(request);

	BOOST_TEST(actual.header_action == expected.header_action);
	BOOST_TEST(actual.header_to == expected.header_to);
	BOOST_TEST(actual.timeout == expected.timeout);
	BOOST_TEST(actual.messages_limit == expected.messages_limit);
}

BOOST_AUTO_TEST_CASE(parse_pullmessages_func_1)
{
	const std::string request =
	"<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\""
		"xmlns:a=\"http://www.w3.org/2005/08/addressing\">"
		"<s:Header>"
        "<a:Action s:mustUnderstand=\"1\">http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest</a:Action>"
        "<a:MessageID>urn:uuid:30cf5aa8-d867-419f-962b-b789f8d7e37e</a:MessageID>"
        "<a:ReplyTo>"
            "<a:Address>http://www.w3.org/2005/08/addressing/anonymous</a:Address>"
        "</a:ReplyTo>"
		"<Security s:mustUnderstand=\"1\""
            "xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
            "<UsernameToken>"
                "<Username>admin</Username>"
                "<Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">24UZInK62uTSnctdnp6ErMpt8LI=</Password>"
                "<Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary\">LYz8rNY2tUyYOTN00Hoo5r0GAAAAAA==</Nonce>"
                "<Created xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">2020-10-27T11:10:42.103Z</Created>"
            "</UsernameToken>"
        "</Security>"
        "<a:To s:mustUnderstand=\"1\">http://192.168.43.120:8000/event_service/0</a:To>"
		"</s:Header>"
		"<s:Body xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
			"xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
			"<PullMessages xmlns=\"http://www.onvif.org/ver10/events/wsdl\">"
				"<Timeout>PT1M</Timeout>"
				"<MessageLimit>1024</MessageLimit>"
			"</PullMessages>"
		"</s:Body>"
	"</s:Envelope>";

	osrv::event::PullMessagesRequest expected;
	expected.header_action = "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest";
	expected.header_to = "http://192.168.43.120:8000/event_service/0";
	expected.timeout = "PT1M";
	expected.messages_limit = 1024;

	auto actual = osrv::event::parse_pullmessages(request);

	BOOST_TEST(actual.header_action == expected.header_action);
	BOOST_TEST(actual.header_to == expected.header_to);
	BOOST_TEST(actual.timeout == expected.timeout);
	BOOST_TEST(actual.messages_limit == expected.messages_limit);
}
*/

BOOST_AUTO_TEST_CASE(compare_subscription_references_func)
{
	using namespace osrv::event;

	const std::string full_ref = "http://127.0.0.1:8080/onvif/event_service/s0";
	const std::string test_subscription_ref = "onvif/event_service/s0";
	const std::string test_subscription_ref2 = "onvif/event_service/s1";

	BOOST_TEST(true == compare_subscription_references(full_ref, test_subscription_ref));
	BOOST_TEST(false == compare_subscription_references(full_ref, test_subscription_ref2));
}

BOOST_AUTO_TEST_CASE(serialize_notification_messages_func0)
{
	// empty queue
	using namespace osrv::event;

	std::deque<NotificationMessage> msgs;
	boost::property_tree::ptree res = serialize_notification_messages(msgs, {});

	auto ctime = exns::find_hierarchy("CurrentTime", res);
	auto ttime = exns::find_hierarchy("TerminationTime", res);

	BOOST_TEST(!ctime.empty());
	BOOST_TEST(!ttime.empty());
}

BOOST_AUTO_TEST_CASE(serialize_notification_messages_func1)
{
	using namespace osrv::event;

	std::deque<NotificationMessage> msgs;

	NotificationMessage test_msg;
	test_msg.source_item_descriptions.push_back({ "ItemName", "ItemValue"});

	msgs.push_back(test_msg);

	boost::property_tree::ptree res = serialize_notification_messages(msgs, {});

	auto name = res.get<std::string>("wsnt:NotificationMessage.wsnt:Message.tt:Message.tt:Source.tt:SimpleItem.<xmlattr>.Name");
	BOOST_TEST(name == "ItemName");

	auto value = res.get<std::string>("wsnt:NotificationMessage.wsnt:Message.tt:Message.tt:Source.tt:SimpleItem.<xmlattr>.Value");
	BOOST_TEST(value == "ItemValue");
}

