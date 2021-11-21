#pragma once

#include "../utility/AuthHelper.h"

#include "../HttpServerFwd.h"

#include <../Simple-Web-Server/server_http.hpp>

#include <string>
#include <map>

namespace osrv
{
	class ServerConfigs;

	struct OnvifRequestBase
	{
		OnvifRequestBase(const std::string& name, osrv::auth::SECURITY_LEVELS lvl,
			const std::map<std::string, std::string>& ns, const std::shared_ptr<pt::ptree>& configs) :
			name_(name)
			, security_level_(lvl)
			, ns_(ns)
			, service_configs_(configs)
		{
		}

		virtual ~OnvifRequestBase() {}

		virtual void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			throw std::exception("Method is not implemented");
		}

		std::string name() const
		{
			return name_;
		}

		osrv::auth::SECURITY_LEVELS security_level()
		{
			return security_level_;
		}

	protected:
		const std::map<std::string, std::string>& ns_;
		const std::shared_ptr<pt::ptree>& service_configs_;

	private:
		//Method name should match the name in the specification
		std::string name_;
		osrv::auth::SECURITY_LEVELS security_level_;
	};
}
