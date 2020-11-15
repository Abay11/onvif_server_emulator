#pragma once

#include "AuthHelper.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <string>
#include <iostream>

#define OVERLOAD_REQUEST_HANDLER void operator()(std::shared_ptr<HttpServer::Response> response, \
	std::shared_ptr<HttpServer::Request> request) override

namespace utility
{
	namespace http
	{

		//HTTP Headers
		extern const char HEADER_AUTHORIZATION[]; //is used in requests
		extern const char HEADER_WWW_AUTHORIZATION[]; //is used in responses

		//HTTP Digest Authentication fields
		extern const char DIGEST_USERNAME[];
		extern const char DIGEST_REALM[];
		extern const char DIGEST_MESSAGE_QOP[];
		extern const char DIGEST_ALGORITHM[];
		extern const char DIGEST_URI[];
		extern const char DIGEST_NONCE[];
		extern const char DIGEST_NONCE_COUNT[];
		extern const char DIGEST_CNONCE[];
		extern const char DIGEST_RESPONSE[];
		extern const char DIGEST_OPAQUE[];

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

		struct RequestHandlerBase
		{
			RequestHandlerBase(const std::string& name, osrv::auth::SECURITY_LEVELS lvl) :
				name_(name),
				security_level_(lvl)
			{
			}

			virtual ~RequestHandlerBase(){}

			virtual void operator()(std::shared_ptr<osrv::HttpServer::Response> response,
				std::shared_ptr<osrv::HttpServer::Request> request)
			{
				throw std::exception("Method is not implemented");
			}

			std::string get_name() const
			{
				return name_;
			}

			osrv::auth::SECURITY_LEVELS get_security_level()
			{
				return security_level_;
			}

		private:
			//Method name should be exactly match the name in the specification
			std::string name_;

			osrv::auth::SECURITY_LEVELS security_level_;
		};
		using HandlerSP = std::shared_ptr<RequestHandlerBase>;
	}
}