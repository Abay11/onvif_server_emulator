#pragma once

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <string>
#include <thread>

class ILogger;

namespace osrv
{
struct ServerConfigs;
namespace rtsp
{

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(ReplayBin, replay_bin, REPLAY, BIN, GstBin);

G_DECLARE_FINAL_TYPE(OnvifFactory, onvif_factory, ONVIF, FACTORY, GstRTSPOnvifMediaFactory);

G_END_DECLS

struct AudioInfo
{
	const std::string codec;
	const unsigned int bitrate;
	const unsigned int samplerate;
};

class Server
{
public:
	Server(ILogger* /*logger*/, ServerConfigs& /*server_configs*/, const AudioInfo& ainfo);
	~Server();
	void run();

private:
	ILogger* logger_;

	GMainLoop* loop_;
	GstRTSPServer* server_;
	GstRTSPMountPoints* mounts_;
	GstRTSPMediaFactory* factoryHighStream_;
	GstRTSPMediaFactory* factoryLowStream_;
	GstRTSPMediaFactory* replayFactory_;

	ServerConfigs* server_configs_ = nullptr;

	std::thread* worker_thread_ = nullptr;
};
} // namespace rtsp
} // namespace osrv