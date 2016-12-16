#ifndef TENGINE_SERVER_HPP
#define TENGINE_SERVER_HPP

#include "asio.hpp"

#include "service_proxy.hpp"
#include "spin_lock.hpp"

#include <memory>
#include <string>
#include <deque>
#include <set>
#include <list>

namespace tengine
{
	class Service;
	class Session;
	class Message;
	class TcpServer;

	typedef std::shared_ptr<Session> SessionPtr;
	typedef std::shared_ptr<TcpServer> TcpServerPtr;

	typedef std::deque<Message> MessageDeque;

	class Socket
	{
	public:
		Socket(asio::ip::tcp::socket& socket)
			: socket_(std::move(socket))
		{}

		virtual ~Socket()
		{}

		virtual void send(const char *data);

	protected:
		asio::ip::tcp::socket socket_;
	};

	class TcpServer : public ServiceProxy
	{
		friend class Session;

	public:
		static constexpr int TCPSERVER_KEY = 0;

		TcpServer(Service *s, short port);

		TcpServer(Service* s, const char * address, short port);

		~TcpServer();

		int start();

		SessionPtr session(int index);

		void close_session(int index);

		void send(int session, const char *data, size_t len);

		std::string local_address();

		std::string address();

		std::string address(int index);

	private:

		enum
		{
			kMaxSessionIndex = 4096,
			kInvalidSessionIndex = 0xFFFFEEEE,
		};

		template<class IndexType>
		class IndexManager
		{
		public:
			typedef IndexType type;

			IndexManager(IndexType start, IndexType size)
				: id_using_()
				, id_usable_()
				, invalid_id_(kInvalidSessionIndex)
			{
				for (IndexType id = start; id < size; id++)
				{
					id_usable_.push_back(id);
				}
			}

			IndexType get()
			{
				if (id_usable_.empty())
					return invalid_id_;

				IndexType id = id_usable_.front();
				id_using_.insert(id);
				id_usable_.pop_front();
				return id;
			}

			void put(IndexType id)
			{
				id_using_.erase(id);

				id_usable_.push_back(id);
			}

		private:
			std::set<IndexType> id_using_;
			std::list<IndexType> id_usable_;
			IndexType invalid_id_;
		};

		SessionPtr create_session();

		void do_accept();

		void asyncNotifyAccept(int session);

		void asyncNotifyRead(int session, const char *data, std::size_t size);

		void asyncNotifyClosed(int session, const char *error);

		asio::strand<asio::executor> executor_;

		asio::ip::tcp::acceptor acceptor_;

		asio::ip::tcp::socket socket_;

		std::string address_;

		std::string port_;

		SpinLock session_lock_;

		typedef std::vector<SessionPtr> SessionPtrPool;

		SessionPtrPool sessions_;

		IndexManager<uint32_t> ids_;
	};


	class UdpServer : public ServiceProxy
	{
	public:
		static constexpr int UDPSERVER_KEY = 0;

		UdpServer(Service *s, short port);

		~UdpServer();

		int start();

		bool join_group(const std::string& multicast_address);

		int send_to(const char* data, std::size_t size,
			const std::string& address, uint16_t port);

		int send_to(const std::string& data, asio::ip::udp::endpoint& endpoint);

		int async_send_to(const char* data, std::size_t size,
			const std::string& address, uint16_t port);

		int async_send_to(const std::string& data,
			asio::ip::udp::endpoint& endpoint);

	private:

		void do_receive();

		void do_write();

		asio::ip::udp::socket socket_;

		short port_;

		std::deque<std::string> messages_;

	};
}



#endif
