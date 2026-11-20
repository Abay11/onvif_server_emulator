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
	configs->journal->Add("info", "system", "application", "Application restart requested");
	response->send([reboot = configs->reboot, journal = configs->journal, logger, pending](const SimpleWeb::error_code& ec) {
		try
		{
			if (!ec)
				reboot();
			else
			{
				journal->Add("warn", "system", "application", "Restart cancelled: response could not be sent");
				logger->Error("Application reboot response failed: " + ec.message());
			}
		}
		catch (const std::exception& e)
		{
			logger->Error(e.what());
			journal->Add("error", "system", "application", "Application restart failed; see process log");
		}
		pending->store(false);
	});
}
} // namespace osrv
