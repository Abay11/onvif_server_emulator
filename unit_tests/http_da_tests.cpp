#include <boost/test/unit_test.hpp>

#include "../utility/HttpDigestHelper.h"


BOOST_AUTO_TEST_CASE(search_value_func)
{

	const std::string request_str = "Digest username=\"admin\", realm=\"iPolis\", qop=\"auth\", algorithm=\"MD5\", uri=\"/onvif/media_service\", nonce=\"5ecdfd69d931557bfa21\", nc=00000001, cnonce=\"5ece0b10ca539fe0e61c\", response=\"85aa20294f742f042f89489cd9fc0ea8\", opaque=\"9652e1db\"";

	std::string username_expected = "admin";
	std::string realm_expected = "iPolis";
	std::string qop_expected = "auth";
	std::string algorithm_expected = "MD5";
	std::string uri_expected = "/onvif/media_service";
	std::string nonce_expected = "5ecdfd69d931557bfa21";
	std::string nc_expected = "00000001";
	std::string cnonce_expected = "5ece0b10ca539fe0e61c";
	std::string response_expected = "85aa20294f742f042f89489cd9fc0ea8";
	std::string opaque_expected = "9652e1db";

	using namespace utility::string;
	BOOST_TEST(search_value(request_str, "digest username") == username_expected);
	BOOST_TEST(search_value(request_str, "realm") == realm_expected);
	BOOST_TEST(search_value(request_str, "qop") == qop_expected);
	BOOST_TEST(search_value(request_str, "algorithm") == algorithm_expected);
	BOOST_TEST(search_value(request_str, "uri") == uri_expected);
	BOOST_TEST(search_value(request_str, "nonce") == nonce_expected);
	BOOST_TEST(search_value(request_str, "nc") == nc_expected);
	BOOST_TEST(search_value(request_str, "cnonce") == cnonce_expected);
	BOOST_TEST(search_value(request_str, "response") == response_expected);
	BOOST_TEST(search_value(request_str, "opaque") == opaque_expected);
}

BOOST_AUTO_TEST_CASE(extract_DA_func)
{
	const std::string request_str = "Digest username=\"admin\", realm=\"iPolis\", qop=\"auth\", algorithm=\"MD5\", uri=\"/onvif/media_service\", nonce=\"5ecdfd69d931557bfa21\", nc=00000001, cnonce=\"5ece0b10ca539fe0e61c\", response=\"85aa20294f742f042f89489cd9fc0ea8\", opaque=\"9652e1db\"";

	using namespace utility::digest;
	DigestRequestHeader expected_result;
	expected_result.username = "admin";
	expected_result.realm = "iPolis";
	expected_result.message_qop = "auth";
	expected_result.algorithm = "MD5";
	expected_result.digest_uri = "/onvif/media_service";
	expected_result.nonce = "5ecdfd69d931557bfa21";
	expected_result.nonce_count = "00000001";
	expected_result.cnonce = "5ece0b10ca539fe0e61c";
	expected_result.response = "85aa20294f742f042f89489cd9fc0ea8";
	expected_result.opaque = "9652e1db";

	auto actual_result = extract_DA(request_str);

	BOOST_TEST(actual_result.username == expected_result.username);
	BOOST_TEST(actual_result.realm == expected_result.realm);
	BOOST_TEST(actual_result.message_qop == expected_result.message_qop);
	BOOST_TEST(actual_result.algorithm == expected_result.algorithm);
	BOOST_TEST(actual_result.digest_uri == expected_result.digest_uri);
	BOOST_TEST(actual_result.nonce == expected_result.nonce);
	BOOST_TEST(actual_result.nonce_count == expected_result.nonce_count);
	BOOST_TEST(actual_result.cnonce == expected_result.cnonce);
	BOOST_TEST(actual_result.response == expected_result.response);
	BOOST_TEST(actual_result.opaque == expected_result.opaque);
}