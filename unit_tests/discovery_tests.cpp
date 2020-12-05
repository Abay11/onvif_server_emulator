#include <boost/test/unit_test.hpp>

#include "../onvif_services/discovery_service.h"
#include "../utility/XmlParser.h"

#include <fstream>
#include <streambuf>
#include <string>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>

BOOST_AUTO_TEST_CASE(extract_message_id_func)
{
	const std::string probe_msg_test = "<?xml version=\"1.0\" encoding=\"utf-8\"?><soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" xmlns:wsd=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" xmlns:wsdp=\"http://schemas.xmlsoap.org/ws/2006/02/devprof\"><soap:Header><wsa:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To><wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action><wsa:MessageID>urn:uuid:4ff5ff0e-8478-4491-a547-e8917023ad90</wsa:MessageID></soap:Header><soap:Body><wsd:Probe><wsd:Types>wsdp:Device</wsd:Types></wsd:Probe></soap:Body></soap:Envelope>";

	auto result = osrv::discovery::utility::extract_message_id(exns::to_ptree(probe_msg_test));

	BOOST_TEST("urn:uuid:4ff5ff0e-8478-4491-a547-e8917023ad90" == result);
}

BOOST_AUTO_TEST_CASE(generate_uuid_func)
{
	auto res1 = osrv::discovery::utility::generate_uuid("urn:uuid:1419d68a-1dd2-11b2-a105-000000000000");
	BOOST_TEST(res1 == "urn:uuid:1419d68a-1dd2-11b2-a105-000000000000");
	
	auto res2 = osrv::discovery::utility::generate_uuid("urn:uuid:1419d68a-1dd2-11b2-a105-000000000000");
	BOOST_TEST(res2 == "urn:uuid:1419d68a-1dd2-11b2-a105-000000000001");
	
	auto res3 = osrv::discovery::utility::generate_uuid("urn:uuid:1419d68a-1dd2-11b2-a105-000000000000");
	BOOST_TEST(res3 == "urn:uuid:1419d68a-1dd2-11b2-a105-000000000002");
}

BOOST_AUTO_TEST_CASE(prepare_resposne_func)
{
	const std::string response_test_file = "../../unit_tests/test_data/discovery_service_test.responses";
	std::ifstream ifs(response_test_file);
	BOOST_TEST(true == ifs.is_open());

	std::string response;
	response.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

	const std::string expected_message_id = osrv::discovery::utility::generate_uuid();
	const std::string expected_relatesTo_id = "urn:uuid:4ff5ff0e-8478-4491-a547-e8917023ad90";

	// this function should replace RelatesTo value in the response
	response = osrv::discovery::utility::prepare_response(expected_message_id, expected_relatesTo_id, std::move(response));

	namespace pt = boost::property_tree;
	std::istringstream is(response);
	pt::ptree response_tree;
	pt::xml_parser::read_xml(is, response_tree);

	auto actual_msg_id = exns::find_hierarchy("Envelope.Header.MessageID", response_tree);
	BOOST_TEST(actual_msg_id == expected_message_id);

	auto actual_related_to = exns::find_hierarchy("Envelope.Header.RelatesTo", response_tree);
	BOOST_TEST(actual_related_to == expected_relatesTo_id);
}