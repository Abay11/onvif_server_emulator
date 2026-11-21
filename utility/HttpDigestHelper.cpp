#include "HttpDigestHelper.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <cctype>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <regex>
#include <sstream>

namespace
{
std::string Hex(const unsigned char* bytes, std::size_t size)
{
	std::string result;
	for (std::size_t i = 0; i < size; ++i)
	{
		result += "0123456789abcdef"[bytes[i] >> 4];
		result += "0123456789abcdef"[bytes[i] & 15];
	}
	return result;
}
std::string Hash(const std::string& bytes, const std::string& algorithm, bool hex = true)
{
	const EVP_MD* md = algorithm == "MD5"						? EVP_md5()
										 : algorithm == "SHA-256"			? EVP_sha256()
										 : algorithm == "SHA-512-256" ? EVP_sha512_256()
										 : algorithm == "SHA-1"				? EVP_sha1()
																									: nullptr;
	if (!md)
		return {};
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	if (EVP_Digest(bytes.data(), bytes.size(), digest, &size, md, nullptr) != 1)
		throw std::runtime_error("Digest calculation failed");
	return hex ? Hex(digest, size) : std::string(reinterpret_cast<char*>(digest), size);
}
bool Equal(const std::string& a, const std::string& b)
{
	return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}
std::string Decode64(std::string text)
{
	std::erase_if(text, [](unsigned char c) { return std::isspace(c); });
	if (text.empty() || text.size() % 4 || text.size() > 4096 ||
			!std::regex_match(text, std::regex("[A-Za-z0-9+/]*={0,2}")))
		throw std::invalid_argument("Invalid base64");
	std::string result(text.size(), '\0');
	int size = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(result.data()),
														 reinterpret_cast<const unsigned char*>(text.data()), text.size());
	if (size < 0)
		throw std::invalid_argument("Invalid base64");
	if (text.back() == '=')
		--size;
	if (text.size() > 1 && text[text.size() - 2] == '=')
		--size;
	result.resize(size);
	return result;
}

// Resolve expanded names at each level; prefixes and default namespaces are caller-controlled.
using Tree = boost::property_tree::ptree;
const Tree Empty;
struct Element
{
	const Tree* tree;
	std::map<std::string, std::string> ns;
	std::string uri, local;
};
Element Expand(const std::string& name, const Tree& tree, std::map<std::string, std::string> ns)
{
	for (const auto& [key, value] : tree.get_child("<xmlattr>", Empty))
		if (key == "xmlns")
			ns[""] = value.data();
		else if (key.starts_with("xmlns:"))
			ns[key.substr(6)] = value.data();
	auto colon = name.find(':');
	auto prefix = colon == std::string::npos ? "" : name.substr(0, colon);
	return {&tree, ns, ns[prefix], colon == std::string::npos ? name : name.substr(colon + 1)};
}
Element Child(const Element& parent, const std::string& uri, const std::string& local, bool optional = false)
{
	Element result{nullptr, {}, {}, {}};
	for (const auto& [key, value] : *parent.tree)
	{
		if (key == "<xmlattr>" || key == "<xmlcomment>")
			continue;
		auto item = Expand(key, value, parent.ns);
		if (item.uri == uri && item.local == local)
		{
			if (result.tree)
				throw std::invalid_argument("Duplicate security element");
			result = item;
		}
	}
	if (!result.tree && !optional)
		throw std::invalid_argument("Missing security element");
	return result;
}
const std::string Soap = "http://www.w3.org/2003/05/soap-envelope";
const std::string Wsse = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd";
const std::string Wsu = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd";
const std::string PasswordBase = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#";
const std::string Base64 =
		"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary";
} // namespace

namespace utility::digest
{
bool hasSecurityHeader(const boost::property_tree::ptree& soap)
{
	try
	{
		Element root{&soap, {}, {}, {}};
		auto envelope = Child(root, Soap, "Envelope");
		auto header = Child(envelope, Soap, "Header", true);
		return header.tree && Child(header, Wsse, "Security", true).tree;
	}
	catch (const std::exception&)
	{
		return true;
	}
}

DigestRequestHeader extract_DA(const std::string& header)
{
	DigestRequestHeader result;
	// Parse auth-params, including escaped quoted strings. Reject duplicates and trailing garbage.
	if (header.size() < 7 || !std::equal(header.begin(), header.begin() + 7, "digest ",
																			 [](unsigned char a, unsigned char b) { return std::tolower(a) == b; }))
		return result;
	std::map<std::string, std::string> fields;
	std::size_t i = 7;
	auto space = [&] {
		while (i < header.size() && std::isspace(static_cast<unsigned char>(header[i])))
			++i;
	};
	while (i < header.size())
	{
		space();
		auto start = i;
		while (i < header.size() &&
					 (std::isalnum(static_cast<unsigned char>(header[i])) || header[i] == '-' || header[i] == '*'))
			++i;
		if (i == start)
			return {};
		auto key = header.substr(start, i - start);
		std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
		space();
		if (i == header.size() || header[i++] != '=')
			return {};
		space();
		std::string value;
		if (i < header.size() && header[i] == '"')
		{
			++i;
			bool closed = false;
			while (i < header.size())
			{
				char c = header[i++];
				if (c == '"')
				{
					closed = true;
					break;
				}
				if (c == '\\')
				{
					if (i == header.size())
						return {};
					c = header[i++];
				}
				if (static_cast<unsigned char>(c) < 32)
					return {};
				value += c;
			}
			if (!closed)
				return {};
		}
		else
		{
			start = i;
			while (i < header.size() && header[i] != ',' && !std::isspace(static_cast<unsigned char>(header[i])))
				++i;
			value = header.substr(start, i - start);
			if (value.empty())
				return {};
		}
		if (!fields.emplace(key, value).second)
			return {};
		space();
		if (i == header.size())
			break;
		if (header[i++] != ',' || i == header.size())
			return {};
	}
	result.username = fields["username"];
	result.realm = fields["realm"];
	result.nonce = fields["nonce"];
	result.digest_uri = fields["uri"];
	result.response = fields["response"];
	result.algorithm = fields["algorithm"].empty() ? "MD5" : fields["algorithm"];
	result.cnonce = fields["cnonce"];
	result.opaque = fields["opaque"];
	result.message_qop = fields["qop"];
	result.nonce_count = fields["nc"];
	return result;
}

DigestResponseHeader DigestSessionImpl::generateDigest()
{
	unsigned char random[32];
	if (RAND_bytes(random, sizeof(random)) != 1)
		throw std::runtime_error("Nonce generation failed");
	DigestResponseHeader result;
	result.realm = realm_;
	result.qop = qop_;
	result.nonce = Hex(random, sizeof(random));
	std::lock_guard lock(mutex_);
	auto now = std::chrono::steady_clock::now();
	std::erase_if(nonce_pool, [&](const auto& item) { return item.second.expires <= now; });
	if (nonce_pool.size() >= 4096)
		nonce_pool.erase(nonce_pool.begin());
	nonce_pool.emplace(result.nonce, NonceState{now + std::chrono::minutes(5), {}});
	return result;
}
std::string IDigestSession::challenges(bool stale)
{
	auto challenge = generateDigest();
	challenge.stale = stale;
	std::string result;
	for (const auto* algorithm : {"MD5", "SHA-256", "SHA-512-256"})
	{
		challenge.algorithm = algorithm;
		result += "WWW-Authenticate: " + challenge.to_string() + "\r\n";
	}
	return result;
}
bool DigestSessionImpl::verifyDigest(const DigestRequestHeader& info, bool& stale)
{
	stale = false;
	auto user = std::find_if(system_users.begin(), system_users.end(),
													 [&](const auto& u) { return u.enabled && u.login == info.username; });
	if (user == system_users.end() || info.realm != realm_ || info.digest_uri != info.target || info.target.empty() ||
			info.method.empty() || info.message_qop != qop_ || info.cnonce.empty() || info.cnonce.size() > 1024 ||
			info.nonce_count.size() != 8 ||
			!std::all_of(info.nonce_count.begin(), info.nonce_count.end(), [](unsigned char c) { return std::isxdigit(c); }))
		return false;
	auto algorithm = info.algorithm;
	bool session = algorithm.ends_with("-sess");
	if (session)
		algorithm.resize(algorithm.size() - 5);
	auto ha1 = Hash(user->login + ":" + realm_ + ":" + user->password, algorithm);
	if (ha1.empty())
		return false;
	if (session)
		ha1 = Hash(ha1 + ":" + info.nonce + ":" + info.cnonce, algorithm);
	auto expected = Hash(ha1 + ":" + info.nonce + ":" + info.nonce_count + ":" + info.cnonce + ":" + qop_ + ":" +
													 Hash(info.method + ":" + info.digest_uri, algorithm),
											 algorithm);
	auto received = info.response;
	std::transform(received.begin(), received.end(), received.begin(), [](unsigned char c) { return std::tolower(c); });
	if (!Equal(expected, received))
		return false;
	std::lock_guard lock(mutex_);
	auto found = nonce_pool.find(info.nonce);
	if (found == nonce_pool.end() || found->second.expires <= std::chrono::steady_clock::now())
	{
		stale = true;
		return false;
	}
	auto count = std::stoul(info.nonce_count, nullptr, 16);
	auto key = info.username + "\n" + info.cnonce;
	auto& counts = found->second.counts;
	if (!count || (counts.contains(key) && count <= counts.at(key)) || (!counts.contains(key) && counts.size() >= 4096))
		return false;
	counts[key] = count;
	return true;
}

std::string DigestSessionImpl::verifyUsernameToken(const Tree& soap)
{
	try
	{
		Element root{&soap, {}, {}, {}};
		auto envelope = Child(root, Soap, "Envelope");
		auto header = Child(envelope, Soap, "Header");
		auto security = Child(header, Wsse, "Security");
		auto token = Child(security, Wsse, "UsernameToken");
		auto username = Child(token, Wsse, "Username").tree->data();
		auto password = Child(token, Wsse, "Password");
		auto nonce = Child(token, Wsse, "Nonce");
		auto created = Child(token, Wsu, "Created").tree->data();
		auto user = std::find_if(system_users.begin(), system_users.end(),
														 [&](const auto& u) { return u.enabled && u.login == username; });
		if (user == system_users.end())
			return {};
		if (nonce.tree->get<std::string>("<xmlattr>.EncodingType", Base64) != Base64)
			return {};
		auto decoded = Decode64(nonce.tree->data());
		// Hash the original timestamp text, but compare its normalized UTC instant.
		std::smatch timestamp;
		if (!std::regex_match(created, timestamp,
													std::regex("([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-2][0-9]:[0-5][0-9]:[0-5][0-9](\\.[0-9]+)?)(Z|[+-]["
																		 "0-9]{2}:[0-5][0-9])")))
			return {};
		if (std::stoi(created.substr(11, 2)) > 23)
			return {};
		auto stamp = boost::posix_time::from_iso_extended_string(timestamp[1]);
		auto zone = timestamp[3].str();
		if (zone != "Z")
		{
			auto hours = std::stoi(zone.substr(1, 2));
			auto minutes = std::stoi(zone.substr(4, 2));
			if (hours > 14 || (hours == 14 && minutes != 0))
				return {};
			auto offset = boost::posix_time::hours(hours) + boost::posix_time::minutes(minutes);
			stamp += zone[0] == '+' ? -offset : offset;
		}
		auto difference = boost::posix_time::microsec_clock::universal_time() - stamp;
		if (difference > boost::posix_time::minutes(5) || difference < -boost::posix_time::seconds(30))
			return {};
		auto type = password.tree->get<std::string>("<xmlattr>.Type", PasswordBase + "PasswordText");
		if (type == PasswordBase + "PasswordDigest")
		{
			if (!Equal(Decode64(password.tree->data()), Hash(decoded + created + user->password, "SHA-1", false)))
				return {};
		}
		else if (type != PasswordBase + "PasswordText" || !Equal(password.tree->data(), user->password))
			return {};
		std::lock_guard lock(mutex_);
		auto now = std::chrono::steady_clock::now();
		std::erase_if(token_nonces, [&](const auto& item) { return item.second <= now; });
		// A nonce is single-use across all users and both SOAP dispatchers.
		if (token_nonces.contains(decoded) || token_nonces.size() >= 16384)
			return {};
		token_nonces.emplace(decoded, now + std::chrono::minutes(6));
		return username;
	}
	catch (const std::exception&)
	{
		return {};
	}
}
} // namespace utility::digest

namespace utility::string
{
std::string search_value(const std::string& source, std::string key)
{
	std::smatch match;
	return std::regex_search(source, match, std::regex(key + "\\s?=\\s?\"?([^,\"\\s]*)\"?", std::regex::icase))
						 ? match[1].str()
						 : "";
}
} // namespace utility::string
