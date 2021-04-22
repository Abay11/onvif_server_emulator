#pragma once


#include "../HttpServerFwd.h"

#include <string>

namespace osrv
{
	class IOnvifService
	{
	public:
	IOnvifService(const std::string& configs_path)
		:
		configs_path_(configs_path)
	{}

	virtual ~IOnvifService() {}

	void Run();

	protected:

		virtual void handleRequest() = 0;

	protected:
		const std::string configs_path_;

	private:
	};

}