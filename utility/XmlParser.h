#pragma once

#include <string>

#include <boost\property_tree\ptree.hpp>

static const char UNEXPECTED_FORMAT[] = "Parsing error: XML content has unexpected format";

static const char ENVELOPE[] = "Envelope";
static const std::string BODY = "Body";
static const char ATTR[] = "<xmlattr>";

//extended xml parser namespace
namespace exns
{

	namespace pt = boost::property_tree;

	//For convinience, all extended methods start with "___"
	class Parser : public pt::ptree
	{
	public:

		//onvif request example
		/*
		std::string xmlContent =
			"<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
			"<s:Body xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
			"xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
			"<GetSystemDateAndTime xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>"
			"</s:Body>"
			"</s:Envelope>";
		*/

		//try to find ONVIF required method in Soap
		//@noNamespace if true, returns value starting after ':'
		std::string ___getMethod(bool noNamespace = true)
		{
			if (begin() == end())
				throw pt::ptree_error(UNEXPECTED_FORMAT);

			using namespace std;

			auto envelopeIt = begin();
			if (envelopeIt->first.find(ENVELOPE) == std::string::npos)
				throw pt::ptree_error(UNEXPECTED_FORMAT);

			//search for Body element
			auto bodyIt = envelopeIt->second.begin();
			auto bodyEndIt = envelopeIt->second.end();
			while (true)
			{
				if (bodyIt == bodyEndIt)
					throw pt::ptree_error(UNEXPECTED_FORMAT);

				if (bodyIt->first == ATTR)
				{
					++bodyIt;
					continue;
				}

				if (bodyIt->first.find(BODY) != std::string::npos);
				{
					break;
				}

				++bodyIt;
			}
			
			//now iterate throw Body elements to find method
			std::string method;
			auto elIt = bodyIt->second.begin();
			auto elEndIt = bodyIt->second.end();
			while (true)
			{
				if (elIt == elEndIt)
					throw pt::ptree_error(UNEXPECTED_FORMAT);

				if (elIt->first == ATTR)
				{
					++elIt;
					continue;
				}

				method = elIt->first;
				break;
			}

			if (noNamespace)
				method = method.substr(method.find(":") + 1);

			return method;
		}
	};
}
