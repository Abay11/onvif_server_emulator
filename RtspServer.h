#pragma once

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <string>
#include <thread>
#include <memory>
#include <vector>

class ILogger;

namespace osrv
{
struct ServerConfigs;
namespace metadata { struct Settings; }
namespace event { class IEventSource; }
namespace rtsp
{
class MetadataStreams;

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
	Server(ILogger* /*logger*/, ServerConfigs& /*server_configs*/, const AudioInfo& ainfo,
			std::shared_ptr<event::IEventSource> event_source = {});
	~Server();
	void run();
	unsigned short Port() const;
	void ConfigureProfile(const std::string& path, const std::string& encoder, std::shared_ptr<const metadata::Settings> settings, bool audio = false);
	void SynchronizeProfile(const std::string& path);
	void RetainProfiles(const std::vector<std::string>& paths);

private:
	ILogger* logger_;
	std::shared_ptr<event::IEventSource> event_source_;

	GMainLoop* loop_;
	GMainContext* context_;
	GstRTSPServer* server_;
	GstRTSPMountPoints* mounts_;
	GstRTSPMediaFactory* factoryHighStream_;
	GstRTSPMediaFactory* factoryLowStream_;
	GstRTSPMediaFactory* replayFactory_;

	ServerConfigs* server_configs_ = nullptr;

	std::thread worker_thread_;
	guint listener_ = 0;
	GSource* session_cleanup_ = nullptr;
	std::unique_ptr<MetadataStreams> metadata_;
	struct Profiles;
	std::shared_ptr<Profiles> profiles_;
	std::string high_pipeline_, low_pipeline_, audio_pipeline_, test_audio_pipeline_;
	void Shutdown();
};
} // namespace rtsp
} // namespace osrv
