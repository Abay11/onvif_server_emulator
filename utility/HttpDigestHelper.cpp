#include "HttpDigestHelper.h"

#include <regex>

#include <cryptopp/md5.h>

namespace utility
{
	namespace digest
	{
		std::string NonceGeneratorConcrete::generate()
		{
			return std::string();
		}

		DigestRequestHeader extract_DA(const std::string& www_auth_line)
		{
			return DigestRequestHeader();
		}
	}

	std::string utility::string::search_value(const std::string& source, std::string key)
	{
		using namespace std;
		
		// key1="value1", key2 = "value2", key3=value3
		key += "\\s?=\\s?\"?([^,\"\\s]*)\"?";
		regex value_regex(key, regex::flag_type::icase);
		smatch matches;
		if (regex_search(source, matches, value_regex))
			return matches[1].str();

		return std::string();
	}

}

