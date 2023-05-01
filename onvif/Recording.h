#pragma once

#include <boost/date_time/posix_time/posix_time.hpp>

#include <string>
#include <set>

namespace osrv
{
	class RecordingEvents;

	class Recording : public std::enable_shared_from_this<Recording>
	{
	public:
		Recording(const std::string& token,
			const std::string& videoTrackToken,
			const std::string& from = {},
			const std::string& until = {});
			
		const std::string& Token() const
		{
			return token_;
		}

		const std::string& VideoTrackToken() const
		{
			return videoTrackToken_;
		}

		boost::posix_time::ptime DateFrom() const;
		boost::posix_time::ptime DateUntil() const;

		std::shared_ptr<RecordingEvents> RecordingEvents();
		

	private:
		const std::string token_;
		const std::string videoTrackToken_;
		const std::string from_;
		const std::string until_;
		boost::posix_time::ptime fixed_from_;
		boost::posix_time::ptime fixed_until_;

		std::shared_ptr<osrv::RecordingEvents> recordingEvents_;
	};

	class RecordingsReaderFromConfig
	{
	public:
		RecordingsReaderFromConfig(const std::string configFile)
			: configFile_(configFile) {}

		std::vector<std::shared_ptr<osrv::Recording>> Recordings();

	private:
		const std::string configFile_;
	};


	struct RecordingEvent
	{
		std::string recordingToken;
		std::string trackToken;
		std::string topic;
		boost::posix_time::ptime utcTime;
		bool isDataPresent;
	};

	class IEventsSearchSession
	{
	public:
		IEventsSearchSession(std::string searchToken,
			boost::posix_time::ptime searchStartPoint, boost::posix_time::ptime searchEndPoint,
			const std::shared_ptr<Recording> relatedRecording)
		: searchToken_(searchToken), searchStartPoint_(searchStartPoint), searchEndPoint_(searchEndPoint), relatedRecording_(relatedRecording) {}
		
		virtual ~IEventsSearchSession() {}

		std::string SearchToken() const
		{
			return searchToken_;
		}

		virtual std::vector<RecordingEvent> Events() = 0;

	protected:
		const std::string searchToken_;
		boost::posix_time::ptime searchStartPoint_;
		boost::posix_time::ptime searchEndPoint_;
		const std::shared_ptr<Recording> relatedRecording_;
	};

	class EventsSearchSessionFactory;

	class RecordingEvents
	{
	public:
		RecordingEvents(std::shared_ptr<Recording> relatedRecording)
			: relatedRecording_(relatedRecording) {}

		std::shared_ptr<IEventsSearchSession> NewSearchSession(boost::posix_time::ptime from, boost::posix_time::ptime until, EventsSearchSessionFactory& factory);
		std::shared_ptr<IEventsSearchSession> NewSearchSession(std::string stringUTCFrom, std::string stringUTCUntil, EventsSearchSessionFactory& factory);
		std::shared_ptr<IEventsSearchSession> NewSearchSession(EventsSearchSessionFactory&& factory);
		std::shared_ptr<IEventsSearchSession> SearchSession(std::string searchToken);


	private:
		std::string SearchToken();

	private:
		std::shared_ptr<Recording> relatedRecording_;

		size_t searchToken_ = 0;

		std::set<std::shared_ptr<IEventsSearchSession>> searchSessions_;
	};

	class SimpleEventsSearchSessionImpl : public IEventsSearchSession
	{
	public:
		SimpleEventsSearchSessionImpl(std::string searchToken,
			boost::posix_time::ptime searchStartPoint, boost::posix_time::ptime searchEndPoint,
			const std::shared_ptr<Recording> relatedRecording)
		: IEventsSearchSession(searchToken, searchStartPoint, searchEndPoint, relatedRecording) {}

		// Inherited via IEventsSearchSession
		virtual std::vector<RecordingEvent> Events() override;
	};

	class EventsSearchSessionFactory
	{
	public:
		EventsSearchSessionFactory(std::string type)
			:type_(type) {}

		std::unique_ptr<IEventsSearchSession> NewSession(std::string searchToken,
			boost::posix_time::ptime searchStartPoint, boost::posix_time::ptime searchEndPoint, std::shared_ptr<Recording> relatedRecording)
		{
			if (type_ == "SimpleEventsSearchSession")
			{
				return std::make_unique<SimpleEventsSearchSessionImpl>(searchToken, searchStartPoint, searchEndPoint, relatedRecording);
			}

			throw std::runtime_error("Unknown IEventsSearchSession implementation");
		}

	private:
		const std::string type_;
	};

	class RecordingsMgr
	{
	public:
		RecordingsMgr(std::string file)
			: file_(file)
		{}

		const std::vector<std::shared_ptr<osrv::Recording>>& Recordings();

	private:
		const std::string file_;

		std::vector<std::shared_ptr<osrv::Recording>> recordings_;
	};
}