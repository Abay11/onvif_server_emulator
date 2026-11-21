#include <boost/test/unit_test.hpp>

#include "../utility/AuthHelper.h"
#include "../utility/HttpDigestHelper.h"

BOOST_AUTO_TEST_CASE(auth_default_policy_covers_every_role_and_access_class)
{
	using namespace osrv::auth;
	struct Row
	{
		SECURITY_LEVELS level;
		bool anonymous, user, oper, admin;
	};
	const Row policy[] = {
			{SECURITY_LEVELS::PRE_AUTH, true, true, true, true},
			{SECURITY_LEVELS::READ_SYSTEM, false, true, true, true},
			{SECURITY_LEVELS::READ_MEDIA, false, true, true, true},
			{SECURITY_LEVELS::READ_SYSTEM_SENSITIVE, false, false, true, true},
			{SECURITY_LEVELS::ACTUATE, false, false, true, true},
			{SECURITY_LEVELS::READ_SYSTEM_SECRET, false, false, false, true},
			{SECURITY_LEVELS::WRITE_SYSTEM, false, false, false, true},
			{SECURITY_LEVELS::UNRECOVERABLE, false, false, false, true},
	};
	for (const auto& row : policy)
	{
		BOOST_TEST(isUserHasAccess(USER_TYPE::ANON, row.level) == row.anonymous);
		BOOST_TEST(isUserHasAccess(USER_TYPE::USER, row.level) == row.user);
		BOOST_TEST(isUserHasAccess(USER_TYPE::OPERATOR, row.level) == row.oper);
		BOOST_TEST(isUserHasAccess(USER_TYPE::ADMIN, row.level) == row.admin);
		BOOST_TEST(!isUserHasAccess(static_cast<USER_TYPE>(255), row.level));
	}
	for (const auto role : {USER_TYPE::ANON, USER_TYPE::USER, USER_TYPE::OPERATOR, USER_TYPE::ADMIN})
		BOOST_TEST(!isUserHasAccess(role, static_cast<SECURITY_LEVELS>(255)));
}

BOOST_AUTO_TEST_CASE(get_usertype_by_username_func)
{
	using namespace osrv::auth;
	std::vector<osrv::auth::UserAccount> users;
	const std::string username = "agent007";
	users.emplace_back(UserAccount{username, "", USER_TYPE::ADMIN});
	auto res = get_usertype_by_username(username, users);
	BOOST_TEST(true == (res == USER_TYPE::ADMIN));

	std::string username2 = "no_such_user";
	auto res2 = get_usertype_by_username(username2, users);
	BOOST_TEST(true == (res2 == USER_TYPE::ANON));
}

#include "../Server.h"
#include "../utility/RequestAuthentication.h"
#include <openssl/evp.h>
using osrv::auth::SECURITY_LEVELS;
using osrv::auth::USER_TYPE;
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace
{
std::string TestHash(const std::string& text, const std::string& algorithm, bool base64 = false)
{
	const auto* md = algorithm == "MD5"				? EVP_md5()
									 : algorithm == "SHA-256" ? EVP_sha256()
									 : algorithm == "SHA-1"		? EVP_sha1()
																						: EVP_sha512_256();
	unsigned char bytes[EVP_MAX_MD_SIZE];
	unsigned size;
	EVP_Digest(text.data(), text.size(), bytes, &size, md, nullptr);
	if (base64)
	{
		std::string output(4 * ((size + 2) / 3) + 1, '\0');
		output.resize(EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()), bytes, size));
		return output;
	}
	std::string output;
	for (unsigned i = 0; i < size; ++i)
	{
		output += "0123456789abcdef"[bytes[i] >> 4];
		output += "0123456789abcdef"[bytes[i] & 15];
	}
	return output;
}
utility::digest::DigestRequestHeader Signed(utility::digest::DigestSessionImpl& session, const std::string& algorithm,
																						const std::string& password = "secret")
{
	utility::digest::DigestRequestHeader h;
	h.username = "test";
	h.realm = "Realm";
	h.nonce = session.generateDigest().nonce;
	h.digest_uri = h.target = "/onvif/device_service?x=1";
	h.method = "POST";
	h.algorithm = algorithm;
	h.message_qop = "auth";
	h.nonce_count = "00000001";
	h.cnonce = "client, nonce";
	auto base = algorithm.ends_with("-sess") ? algorithm.substr(0, algorithm.size() - 5) : algorithm;
	auto ha1 = TestHash("test:Realm:" + password, base);
	if (algorithm.ends_with("-sess"))
		ha1 = TestHash(ha1 + ":" + h.nonce + ":" + h.cnonce, base);
	h.response = TestHash(ha1 + ":" + h.nonce + ":" + h.nonce_count + ":" + h.cnonce +
														":auth:" + TestHash("POST:" + h.digest_uri, base),
												base);
	return h;
}
boost::property_tree::ptree Token(
		const std::string& password = "secret", const std::string& age = "", bool text = false,
		const std::string& ns = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd")
{
	auto created =
			age.empty() ? boost::posix_time::to_iso_extended_string(boost::posix_time::microsec_clock::universal_time()) + "Z"
									: age;
	auto digest = text ? password : TestHash("nonce" + created + password, "SHA-1", true);
	std::istringstream xml(
			"<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\"><e:Header>"
			"<Security xmlns=\"" +
			ns +
			"\"><UsernameToken><Username>test</Username>"
			"<Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#Password" +
			(text ? "Text" : "Digest") + "\">" + digest +
			"</Password><Nonce>bm9uY2U=</Nonce>"
			"<u:Created xmlns:u=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">" +
			created + "</u:Created></UsernameToken></Security></e:Header><e:Body/></e:Envelope>");
	boost::property_tree::ptree tree;
	boost::property_tree::read_xml(xml, tree);
	return tree;
}
} // namespace
BOOST_AUTO_TEST_CASE(digest_all_algorithms_validate_password_method_uri_and_replay)
{
	for (auto algorithm : {"MD5", "SHA-256", "SHA-512-256", "MD5-sess", "SHA-256-sess", "SHA-512-256-sess"})
	{
		utility::digest::DigestSessionImpl session;
		session.set_users_list({{"test", "secret", USER_TYPE::ADMIN}});
		bool stale = true;
		auto h = Signed(session, algorithm);
		auto bad = h;
		bad.method = "GET";
		BOOST_TEST(!session.verifyDigest(bad, stale));
		bad = h;
		bad.target = "/elsewhere";
		BOOST_TEST(!session.verifyDigest(bad, stale));
		bad = h;
		bad.realm = "other";
		BOOST_TEST(!session.verifyDigest(bad, stale));
		bad = Signed(session, algorithm, "incorrect");
		BOOST_TEST(!session.verifyDigest(bad, stale));
		BOOST_TEST(session.verifyDigest(h, stale));
		BOOST_TEST(!stale);
		BOOST_TEST(!session.verifyDigest(h, stale));
		session.set_users_list({{"test", "secret", USER_TYPE::ADMIN, false}});
		BOOST_TEST(!session.verifyDigest(Signed(session, algorithm), stale));
	}
}
BOOST_AUTO_TEST_CASE(digest_challenges_are_ordered_and_parser_is_strict)
{
	utility::digest::DigestSessionImpl session;
	auto headers = session.challenges();
	BOOST_TEST(headers.find("algorithm=MD5") < headers.find("algorithm=SHA-256"));
	BOOST_TEST(headers.find("algorithm=SHA-256") < headers.find("algorithm=SHA-512-256"));
	BOOST_TEST(session.generateDigest().nonce != session.generateDigest().nonce);
	auto h = utility::digest::extract_DA("Digest username=\"user name\", cnonce=\"a,b\\\"c\", algorithm=SHA-256");
	BOOST_TEST(h.username == "user name");
	BOOST_TEST(h.cnonce == "a,b\"c");
	BOOST_TEST(utility::digest::extract_DA("Basic username=\"test\"").username.empty());
	BOOST_TEST(utility::digest::extract_DA("Digest username=\"test\", username=\"other\"").username.empty());
}
BOOST_AUTO_TEST_CASE(username_token_checks_digest_namespaces_freshness_and_replay)
{
	utility::digest::DigestSessionImpl session;
	session.set_users_list({{"test", "secret", USER_TYPE::ADMIN}});
	BOOST_TEST(session.verifyUsernameToken(Token("wrong")).empty());
	BOOST_TEST(session.verifyUsernameToken(Token("secret", "2000-01-01T00:00:00Z")).empty());
	BOOST_TEST(session.verifyUsernameToken(Token("secret", "", false, "urn:wrong")).empty());
	auto token = Token();
	BOOST_TEST(session.verifyUsernameToken(token) == "test");
	BOOST_TEST(session.verifyUsernameToken(token).empty());
	utility::digest::DigestSessionImpl textSession;
	textSession.set_users_list({{"test", "secret", USER_TYPE::ADMIN}});
	BOOST_TEST(textSession.verifyUsernameToken(Token("secret", "", true)) == "test");
	utility::digest::DigestSessionImpl disabled;
	disabled.set_users_list({{"test", "secret", USER_TYPE::ADMIN, false}});
	BOOST_TEST(disabled.verifyUsernameToken(Token()).empty());
}
BOOST_AUTO_TEST_CASE(auth_rejection_emits_soap_fault)
{
	osrv::ServerConfigs config;
	std::ostringstream response;
	utility::auth::Reject(response, config, utility::auth::Rejected(true));
	BOOST_TEST(response.str().starts_with("HTTP/1.1 400"));
	BOOST_TEST(response.str().find("ter:NotAuthorized") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(digest_independent_vectors_and_expired_nonce)
{
	struct Session : utility::digest::DigestSessionImpl
	{
		void Seed(bool expired = false)
		{
			nonce_pool["server-nonce"] = {
					std::chrono::steady_clock::now() + (expired ? -std::chrono::minutes(1) : std::chrono::minutes(1)), {}};
		}
	};
	// Independent fixed vectors computed with Python hashlib, not the server implementation.
	for (const auto& [algorithm, response] : std::vector<std::pair<std::string, std::string>>{
					 {"MD5", "64c86c906db2fd4c9feef62413ea191a"},
					 {"SHA-256", "0c199f807e86effa30888cc6abb4bbd11241de14e66497bf70768b431a486a82"},
					 {"SHA-512-256", "3d28d4e59da1de5bf115847cf0e51770bfd04e08e9092cc6392be8ff669ec9e7"}})
	{
		Session session;
		session.set_users_list({{"test", "secret", USER_TYPE::ADMIN}});
		session.Seed();
		auto h = utility::digest::extract_DA("Digest username=\"test\", realm=\"Realm\", nonce=\"server-nonce\", "
																				 "uri=\"/fixed\", algorithm=" +
																				 algorithm + ", qop=auth, nc=00000001, cnonce=\"client\", response=\"" +
																				 response + "\"");
		h.method = "POST";
		h.target = "/fixed";
		bool stale = false;
		BOOST_TEST(session.verifyDigest(h, stale));
		session.Seed(true);
		BOOST_TEST(!session.verifyDigest(h, stale));
		BOOST_TEST(stale);
		BOOST_TEST(session.challenges(true).find("stale=true") != std::string::npos);
	}
}

BOOST_AUTO_TEST_CASE(authorization_denial_is_a_soap_fault_without_digest_challenge)
{
	for (const auto role : {USER_TYPE::ANON, USER_TYPE::USER, USER_TYPE::OPERATOR})
	{
		try
		{
			utility::auth::Authorize(role, SECURITY_LEVELS::UNRECOVERABLE);
			BOOST_FAIL("A non-administrator was authorized to reboot");
		}
		catch (const utility::auth::Rejected& error)
		{
			osrv::ServerConfigs config;
			std::ostringstream output;
			utility::auth::Reject(output, config, error);
			BOOST_TEST(output.str().starts_with("HTTP/1.1 400 Bad Request"));
			BOOST_TEST(output.str().find("WWW-Authenticate") == std::string::npos);
			std::istringstream content(output.str().substr(output.str().find("\r\n\r\n") + 4));
			boost::property_tree::ptree fault;
			boost::property_tree::read_xml(content, fault);
			BOOST_TEST(fault.get<std::string>("s:Envelope.s:Body.s:Fault.s:Code.s:Value") == "s:Sender");
			BOOST_TEST(fault.get<std::string>("s:Envelope.s:Body.s:Fault.s:Code.s:Subcode.s:Value") == "ter:NotAuthorized");
		}
	}
	BOOST_CHECK_NO_THROW(utility::auth::Authorize(USER_TYPE::ADMIN, SECURITY_LEVELS::UNRECOVERABLE));
	BOOST_CHECK_NO_THROW(utility::auth::Authorize(USER_TYPE::OPERATOR, SECURITY_LEVELS::ACTUATE));
	BOOST_CHECK_THROW(utility::auth::Authorize(USER_TYPE::USER, SECURITY_LEVELS::ACTUATE), utility::auth::Rejected);
}
