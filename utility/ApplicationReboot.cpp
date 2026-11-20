#include "ApplicationReboot.h"
#include "../Server.h"
#include "../Simple-Web-Server/server_http.hpp"

namespace osrv
{
void RebootAfterResponse(const std::shared_ptr<HttpServer::Response>& response,
												 const std::shared_ptr<ServerConfigs>& configs, const std::shared_ptr<ILogger>& logger)
{
	response->close_connection_after_response = true;
	auto pending = configs->reboot_pending;
	if (pending->exchange(true))
		return;
	response->send([reboot = configs->reboot, logger, pending](const SimpleWeb::error_code& ec) {
		try
		{
			if (!ec)
				reboot();
			else
				logger->Error("Application reboot response failed: " + ec.message());
		}
		catch (const std::exception& e)
		{
			logger->Error(e.what());
		}
		pending->store(false);
	});
}
} // namespace osrv
