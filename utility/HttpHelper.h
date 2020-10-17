#pragma once

#include "AuthHelper.h"

#include <string>
#include <iostream>

namespace utility
{
	namespace http
	{
		//HTTP Headers
		extern const char HEADER_AUTHORIZATION[];

		//HTTP Responses
		extern const char RESPONSE_UNAUTHORIZED[];

		struct UserCredentials
		{
			UserCredentials()
				:type(osrv::auth::USER_TYPE::ANON)
			{}

			std::string login;
			std::string password;
			osrv::auth::USER_TYPE type;
		};

		//NOTE: this structure is not usefull at current time after refactoring
		//maybe should be removed entirely
		struct SessionInfoHolder
		{
			UserCredentials user_creds;
		};

		using HeadersWriter = void(std::ostream&, const std::string&);

		inline void NoErrorDefaultWriter(std::ostream& os, const std::string& content)
		{
			os << "HTTP/1.1 200 OK\r\n"
				<< "Content-Type: application/soap+xml; charset=utf-8\r\n"
				<< "Content-Length: " << content.length() << "\r\n"
				<< "Connection: close"
				<< "\r\n\r\n"
				<< content;
		}

		inline void ClientErrorDefaultWriter(std::ostream& os, const std::string& content)
		{
			os << "HTTP/1.1 400 OK\r\n"
				<< "Content-Type: application/soap+xml; charset=utf-8\r\n"
				<< "Content-Length: " << content.length() << "\r\n"
				<< "Connection: close"
				<< "\r\n\r\n"
				<< content;
		}

		inline void fillResponseWithHeaders(std::ostream& os, const std::string& content, HeadersWriter* writer = NoErrorDefaultWriter)
		{
			writer(os, content);
		}
	}
}