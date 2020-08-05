#include <iostream>

//#include <http_parser/http_parser.h>
#include <boost/asio.hpp>

const std::string MASTER_PORT = "8080";
const std::string MASTER_ADDR = "127.0.0.1";

//should be used only one instance of this class per the whole programm lifecicle
//threadsafe
class Logger
{
public:

	static const int LVL_ERR = 0;
	static const int LVL_WARN = 1;
	static const int LVL_INFO = 2;
	static const int LVL_DEBUG = 3;

	Logger(int loggingLevel = LVL_INFO)
		: m_loggingLevel(loggingLevel)
	{
	}

	void Info(const std::string& msg)
	{
		write(msg, LVL_INFO);
	}

	void Warn(const std::string& msg)
	{
		write(msg, LVL_WARN);
	}

	void Error(const std::string& msg)
	{
		write(msg, LVL_ERR);
	}

private:
	void write(const std::string& msg, int level)
	{
		if (level > m_loggingLevel)
			return;

		std::string decorated_msg;

		switch (level)
		{
		case LVL_ERR:
			decorated_msg += "ERR: ";
			break;
		case LVL_WARN:
			decorated_msg += "WARN: ";
			break;
		case LVL_INFO:
			decorated_msg += "INFO: ";
			break;
		case LVL_DEBUG:
			decorated_msg += "DEBUG: ";
			break;
		}

		decorated_msg += msg;
		
		std::lock_guard<std::mutex> lg(log_writer_mutex);
		std::cout << decorated_msg << std::endl;
	}

private:
	std::mutex log_writer_mutex;

	int m_loggingLevel;
};

auto async_accept_handler = [](boost::system::error_code ec,
	boost::asio::ip::tcp::socket socket,
	boost::asio::io_context& ctx_,
	boost::asio::ip::tcp::acceptor& acceptor_)
{
	socket.close();
			// Check whether the server was stopped by a signal before this
			// completion handler had a chance to run.
			if (!acceptor_.is_open())
			{
				return;
			}

			if (!ec)
			{
				//socket.async_read_some();
			}

//	return acceptor_.async_accept(std::bind(async_accept_handler, _1, _2, std::ref(ctx_)));
};

int main()
{
	using namespace std;

	using namespace boost::asio;

	Logger log(Logger::LVL_DEBUG);

	//// STARTING SERVER
	const int CONCURRENCY_HINT = 1;
	boost::asio::io_context ctx_(CONCURRENCY_HINT);

	/// Acceptor used to listen for incoming connections.
	ip::tcp::acceptor acceptor_(ctx_);
	ip::tcp::resolver resolver_(ctx_);
	boost::asio::ip::tcp::endpoint endpoint =
    *resolver_.resolve(MASTER_ADDR, MASTER_PORT).begin();

	try
	{
		acceptor_.open(endpoint.protocol());
	}
	catch (const boost::system::system_error& e)
	{
		std::string msg = "Can't start listen the master port ";
		msg += e.what();
		log.Error(msg);

		return -1;
	}

	acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
	acceptor_.bind(endpoint);
	acceptor_.listen();

	log.Info("Server is successfully started");

	using namespace std::placeholders;
	acceptor_.async_accept(std::bind(async_accept_handler, _1, _2, std::ref(ctx_), std::ref(acceptor_)));

	ctx_.run();

	cout << "OK" << endl;
}
