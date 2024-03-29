#pragma once

#include <string>

#include <boost\property_tree\ptree.hpp>

static const char UNEXPECTED_FORMAT[] = "Parsing error: XML content has unexpected format";

static const std::string ENVELOPE = "Envelope";
static const std::string BODY = "Body";
static const std::string ATTR = "<xmlattr>";

// extended xml parser namespace
namespace exns
{

namespace pt = boost::property_tree;

// For convinience, all extended methods start with "___"
class Parser : public pt::ptree
{
public:
	// onvif request examples
	/*
	std::string xmlContent =
		"<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
		"<s:Body xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
		"xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
		"<GetSystemDateAndTime xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>"
		"</s:Body>"
		"</s:Envelope>";
	*/

	/*
	<s:Envelope xmlns : s = "http://www.w3.org/2003/05/soap-envelope">
		<s:Header>
		<Security s : mustUnderstand = "1"
		xmlns = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd">
		<UsernameToken>
		<Username>admin< / Username>
		<Password Type =
	"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest">BSzehQXlkG0oGjFMOMCWfEYH2EQ
	= < / Password> <Nonce EncodingType =
	"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary">SEoWHX1ObUmS7 +
	oMVYUGWQIAAAAAAA == < / Nonce> < Created xmlns =
	"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd">2020 - 08 - 22T12:26 : 23.693Z< /
	Created> < / UsernameToken> < / Security> < / s : Header> <s : Body xmlns : xsi =
	"http://www.w3.org/2001/XMLSchema-instance" xmlns : xsd = "http://www.w3.org/2001/XMLSchema"> <GetScopes xmlns =
	"http://www.onvif.org/ver10/device/wsdl" / > < / s:Body> < / s:Envelope>
	*/

	// try to find ONVIF required method in Soap
	//@noNamespace if true, returns value starting after ':'
	std::string ___getMethod(bool noNamespace = true)
	{
		if (begin() == end())
			throw pt::ptree_error(UNEXPECTED_FORMAT);

		using namespace std;

		auto envelope_tree = begin();
		if (envelope_tree->first.find(ENVELOPE) == std::string::npos) // it's strictly condition
			throw pt::ptree_error(UNEXPECTED_FORMAT);

		// search for Body element
		auto body_tree = envelope_tree->second.begin();
		auto body_end = envelope_tree->second.end();
		while (true)
		{
			if (body_tree == body_end)
				throw pt::ptree_error(UNEXPECTED_FORMAT);

			if (body_tree->first.find(BODY) != std::string::npos)
			{
				break;
			}

			++body_tree;
		}

		// now iterate throw Body elements to find method
		std::string method;
		auto method_tree = body_tree->second.begin();
		auto method_end = body_tree->second.end();
		while (true)
		{
			if (method_tree == method_end)
				throw pt::ptree_error(UNEXPECTED_FORMAT);

			if (method_tree->first == ATTR)
			{
				++method_tree;
				continue;
			}

			method = method_tree->first;
			break;
		}

		if (noNamespace)
			method = method.substr(method.find(":") + 1);

		return method;
	}
};

// do the same thing as ptree::find member class, different in that, this method searchs ignoring
// XML NS preffix if it even is present
pt::ptree::const_assoc_iterator find(const pt::ptree::key_type& /*key*/, const pt::ptree& /*node*/);

// Do the same as exns::find(), but return a value as a string and accept a whole hierarchy for searching,
// Expected format is same as the way in Boost::Property_tree - childs is dot separated:  ex: "el1.el2.el3"
// TODO: fix searching also attribute elements
std::string find_hierarchy(const std::string& /*path*/, const pt::ptree& /*node*/);

// same as \find_hierarchy, but this version could be used to get multiple childrens with the same name.
// return empty list if could not find any elements
std::vector<pt::ptree::const_iterator> find_hierarchy_elements(std::string_view /*path*/, const pt::ptree& /*root*/);

pt::ptree to_ptree(const std::string& str);
} // namespace exns
