#ifndef TENGINE_CHANNEL_HPP
#define TENGINE_CHANNEL_HPP

#include "asio.hpp"

#include "message.hpp"
#include "service_proxy.hpp"
#include "allocator.hpp"

#include <string>
#include <deque>
#include <memory>

namespace tengine
{
	class Service;

	class Channel;

	typedef std::shared_ptr<Channel> ChannelPtr;

	class Channel :
		public ServiceProxy, public std::enable_shared_from_this<Channel>
	{
	public:

		static constexpr int CHANNEL_KEY = 0;

		Channel(Service *s);

		~Channel();

		void connect(const char *address, const char *port);

		void async_connect(const char *address, const char *port);

		void write(const char *data, size_t size);

		void write(const Message& message);

		void close();

		bool is_open();

	private:
		void do_connect(
			const asio::ip::tcp::resolver::iterator& endpoint_iterator);

		void do_read_header();

		void do_read_body();

		void do_write();

		void asyncNotifyConnected();

		void asyncNotifyRead(const char *data, std::size_t size);

		void asyncNotifyClosed(const char *error);

		asio::io_service& io_service_;

		asio::ip::tcp::socket socket_;

		std::string address_;

		std::string port_;

		Message read_message_;

		MessageDeque write_messages_;

		HandlerAllocator allocator_;
	};

	//////////////////////////////////////////////////////////////////////////////
	class UdpChannel;

	typedef std::shared_ptr<UdpChannel> UdpChannelPtr;

	class UdpChannel :
		public ServiceProxy, public std::enable_shared_from_this<UdpChannel>
	{
	public:

		static constexpr int UDPCHANNEL_KEY = 0;

		UdpChannel(Service *s) = delete;

		UdpChannel(
			Service *s, const std::string& host, const std::string& port);

		~UdpChannel();

		int send_to(const char* data, std::size_t size);

		int send_to(const std::string& data);

		int async_send_to(const char* data, std::size_t size);

		int async_send_to(const std::string& data);

		void close();

	private:

		struct MessageData {
			std::string data;
		};

		void do_write();

		void do_write(const MessageData& data);

		void do_receive();

		asio::io_service& io_service_;

		asio::ip::udp::socket socket_;

		asio::ip::udp::endpoint endpoint_;

		std::deque<MessageData> message_holder_;
	};

	//////////////////////////////////////////////////////////////////////////////
	class UdpSender;

	typedef std::shared_ptr<UdpSender> UdpSenderPtr;

	class UdpSender :
		public ServiceProxy, public std::enable_shared_from_this<UdpSender>
	{
	public:

		static constexpr int UDPSENDER_KEY = 0;

		UdpSender(Service *s);

		~UdpSender();

		int send_to(const char* data, std::size_t size,
			const std::string& address, uint16_t port);

		int send_to(const std::string& data,
			asio::ip::udp::endpoint& endpoint);

		int async_send_to(const char* data, std::size_t size,
			const std::string& address, uint16_t port);

		int async_send_to(const std::string& data,
			asio::ip::udp::endpoint& endpoint);

		void close();

	private:
		struct MessageData {
			std::string data;
			asio::ip::udp::endpoint endpoint;
		};

		void do_write();

		void do_write(const MessageData& data);

		void do_receive();

		asio::io_service& io_service_;

		asio::ip::udp::socket socket_;

		std::deque<MessageData> message_holder_;
	};

}

#endif
