#include "server.hpp"

#include "executor.hpp"
#include "context.hpp"
#include "service.hpp"
#include "message.hpp"
#include "dispatch.hpp"

#include "asio/ts/executor.hpp"

#include <memory>
#include <iostream>

namespace tengine
{
	class Session : public std::enable_shared_from_this<Session>
	{
	public:
		Session(TcpServer& s, asio::ip::tcp::socket socket, uint32_t index)
			: owner_(s)
			, socket_(std::move(socket))
			, index_(index)
		{
		}

		~Session()
		{
			close();
		}

		void start()
		{
			do_read_header();
		}

		void close()
		{
			if (!socket_.is_open())
				return;

			asio::error_code ignored_ec;
			socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
			socket_.close(ignored_ec);
		}

		void write(const char *data, size_t len)
		{
			Message msg;
			msg.body_length((uint16_t)len);
			std::memcpy(msg.body(), data, msg.body_length());
			msg.encode_header();
			send(msg);
		}

		void send(const Message& msg)
		{
			do_write(msg);
			/*
			bool write_in_progress = !write_msgs_.empty();
			write_msgs_.push_back(msg);
			if (!write_in_progress)
			{
				do_write();
			}
			*/
		}

		const std::string remote_address()
		{
			try
			{
				asio::ip::tcp::endpoint ep = socket_.remote_endpoint();
				return ep.address().to_string();
			}
			catch (asio::error_code& /*error*/)
			{
				return "unknown";
			}
		}

		uint32_t index()
		{
			return index_;
		}

	private:
		void do_read_header()
		{
			auto self(shared_from_this());
			asio::async_read(socket_,
				asio::buffer(read_msg_.data(),
					Message::header_length + Message::reserve_length),
				[this, self](std::error_code ec, std::size_t /*length*/)
				{
					if (!ec && read_msg_.decode_header())
					{
						do_read_body();
					}
					else
					{
						owner_.asyncNotifyClosed(index_, ec.message().c_str(), ec.message().size());
					}
				}
			);
		}

		void do_read_body()
		{
			auto self(shared_from_this());
			asio::async_read(socket_,
				asio::buffer(read_msg_.body(), read_msg_.body_length()),
				[this, self](std::error_code ec, std::size_t /*length*/)
				{
					if (!ec)
					{
						owner_.asyncNotifyRead(
							index_, read_msg_.body(), read_msg_.body_length());

						do_read_header();
					}
					else
					{
						owner_.asyncNotifyClosed(index_, ec.message().c_str(), ec.message().size());
					}
				}
			);
		}

		void do_write()
		{
			auto self(shared_from_this());
			asio::async_write(socket_,
				asio::buffer(write_msgs_.front().data(),
					write_msgs_.front().length()),
				[this, self](std::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						write_msgs_.pop_front();
						if (!write_msgs_.empty())
						{
							do_write();
						}
					}
					else
					{
						owner_.asyncNotifyClosed(index_, ec.message().c_str(), ec.message().size());
					}
				}
			);
		}

		void do_write(const Message& msg)
		{
			auto self(shared_from_this());
			asio::async_write(socket_,
				asio::buffer(msg.data(), msg.length()),
				[this, self](std::error_code ec, std::size_t length)
				{
					if (ec)
					{
						owner_.asyncNotifyClosed(index_, ec.message().c_str(), ec.message().size());
					}
				}
			);
		}

		TcpServer& owner_;
		asio::ip::tcp::socket socket_;
		Message read_msg_;
		MessageDeque write_msgs_;
		uint32_t index_;
	};

	constexpr int TcpServer::TCPSERVER_KEY;

	TcpServer::TcpServer(Service* s, short port)
		: TcpServer(s, "0.0.0.0", port)
	{
	}


	TcpServer::TcpServer(Service* s, const char * address, short port)
		: ServiceProxy(s)
		, executor_(s->context().executor())
		, acceptor_(s->context().net_executor().io_service())
		, socket_(s->context().net_executor().io_service())
		, address_(address)
		, port_(std::to_string(port))
		, sessions_()
		, ids_(0, kMaxSessionIndex)
	{
		sessions_.resize(kMaxSessionIndex);

		asio::ip::tcp::resolver resolver(s->context().net_executor().io_service());
		asio::ip::tcp::endpoint endpoint =
			*resolver.resolve({ address_, port_ });
		acceptor_.open(endpoint.protocol());

		acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
		acceptor_.set_option(asio::ip::tcp::no_delay(true));

		acceptor_.bind(endpoint);
		acceptor_.listen();

		do_accept();
	}

	TcpServer::~TcpServer()
	{
		acceptor_.close();

		SpinHolder holder(session_lock_);

		for (std::size_t i = 0; i < sessions_.size(); i++)
		{
			if (sessions_[i])
			{
				sessions_[i]->close();
				uint32_t id = sessions_[i]->index();
				ids_.put(id);
				sessions_[i].reset();
			}
		}

		sessions_.clear();
	}

	int TcpServer::start()
	{
		do_accept();
		return 0;
	}

	SessionPtr TcpServer::session(int index)
	{
		SpinHolder holder(session_lock_);

		if (index < 0 || index > (int)sessions_.size())
			return SessionPtr();

		return sessions_[index];
	}

	void TcpServer::send(int session, const char *data, size_t len)
	{
		SessionPtr session_ptr = this->session(session);
		if (session_ptr)
			session_ptr->write(data, len);
	}

	void TcpServer::close_session(int index)
	{
		SpinHolder holder(session_lock_);

		if (index < 0 || index >= (int)sessions_.size())
			return;

		SessionPtr ptr = sessions_[index];

		if (ptr)
		{
			ptr->close();
			uint32_t id = ptr->index();
			ids_.put(id);
		}

		sessions_[index].reset();
	}

	std::string TcpServer::local_address()
	{
		asio::ip::tcp::endpoint ep = acceptor_.local_endpoint();

		std::stringstream os;

		os << address() << ":" << ep.port();

		return os.str();
	}

	std::string TcpServer::address()
	{
		/*
		asio::error_code ec;
		asio::ip::tcp::endpoint ep = acceptor_.local_endpoint(ec);
		if (!ec)
		return ep.address().to_string();

		return "unknown";
		*/
		try
		{
			asio::ip::tcp::resolver resolver(host_->context().io_service());
			//asio::ip::tcp::resolver::query query(asio::ip::tcp::v4(),
			//	asio::ip::host_name(), asio::ip::resolver_query_base::flags());

			asio::ip::tcp::resolver::query query(asio::ip::tcp::v4(),
				asio::ip::host_name(), "");

			asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);

			/*
			asio::ip::tcp::resolver::iterator end;
			while (iter != end)
			{
			asio::ip::tcp::endpoint ep = *iter++;
			}
			*/

			asio::ip::tcp::endpoint ep;
			asio::ip::tcp::resolver::iterator end;
			if (iter != end)
				ep = *iter++;

			return ep.address().to_v4().to_string();
		}
		catch (const std::system_error&)
		{
			return "unknown";
		}
	}

	std::string TcpServer::address(int index)
	{
		SessionPtr session = this->session(index);
		if (session)
			return session->remote_address();

		return "";
	}

	SessionPtr TcpServer::create_session()
	{
		SpinHolder holder(session_lock_);

		uint32_t index = ids_.get();

		SessionPtr ptr(new Session(*this, std::move(socket_), index));

		sessions_[index] = ptr;

		return ptr;
	}

	void TcpServer::do_accept()
	{
		acceptor_.async_accept(socket_,
			[this](std::error_code ec)
			{
				if (!acceptor_.is_open())
					return;

				if (!ec)
				{
					SessionPtr session = create_session();

					if (session)
					{
						session->start();

						asyncNotifyAccept(session->index());
					}

				}

				do_accept();
			}
		);
	}

	void TcpServer::asyncNotifyAccept(int session)
	{
		dispatch<MessageType::kMessageTcpServerAccept, SandBox>(
			host(), host(), (void*)this, session);
	}

	void TcpServer::asyncNotifyRead(
		int session, const char *data, std::size_t size)
	{
		const char *tmp = (char*)ccmalloc(size);

		if (tmp)
		{
			memcpy((void*)tmp, data, size);

			dispatch<MessageType::kMessageTcpServerRead, SandBox>(
				host(), host(), (void*)this, session, tmp, size);
		}
	}

	void TcpServer::asyncNotifyClosed(int session, const char *error, std::size_t size)
	{
		this->close_session(session);

		char *msg = (char*)ccmalloc(size+1);
		if (msg)
		{
			memcpy(msg, error, size);
			msg[size] = '\0';
			dispatch<MessageType::kMessageTcpServerClosed, SandBox>(
				host(), host(), (void*)this, session, (const char*)msg);
		}
	}

	///////////////////////////////////////////////////////////////////////////
		constexpr int UdpServer::UDPSERVER_KEY;

    UdpServer::UdpServer(Service* s, short port)
        : ServiceProxy(s)
		, socket_(s->context().net_executor().io_service())
		, port_(port)
		, messages_()
	{

	}

	UdpServer::~UdpServer()
	{
		socket_.close();
	}

	int UdpServer::start()
	{
		asio::ip::udp::endpoint listen_endpoint(
			asio::ip::address::from_string("0.0.0.0"), port_);
		socket_.open(listen_endpoint.protocol());
		socket_.set_option(asio::ip::udp::socket::reuse_address(true));
		socket_.bind(listen_endpoint);

		do_receive();

		return 0;
	}

	bool UdpServer::join_group(const std::string& multicast_address)
	{
		if (!socket_.is_open())
			return false;

		socket_.set_option(
			asio::ip::multicast::join_group(
				asio::ip::address::from_string(multicast_address)));

		return true;
	}

	void UdpServer::do_receive()
	{
		socket_.async_receive(asio::null_buffers(),
			[this](const asio::error_code& error, unsigned int)
		{
			if (error)
			{
				std::cerr << error.message() << std::endl;
			}
			else
			{
				size_t bytes_available = socket_.available();

				auto receive_buffer =
					std::make_shared<std::vector<char>>(bytes_available);
				auto sender_endpoint =
					std::make_shared<asio::ip::udp::endpoint>();

				socket_.async_receive_from(
					asio::buffer(receive_buffer->data(),
						receive_buffer->size()), *sender_endpoint,
					[this, receive_buffer, sender_endpoint]
				(const asio::error_code& error, std::size_t bytes_recvd)
				{
					if (error)
					{
						std::cerr << error.message() << std::endl;
					}
					else
					{
						const std::string& address = sender_endpoint->address().to_string();
						uint16_t port = sender_endpoint->port();

						dispatch<MessageType::kMessageUdpServerRead, SandBox>(host(), host(),
							(void*)this, address, port, (const char*)receive_buffer->data(), bytes_recvd);

						do_receive();
					}
				});
			}
		});
	}

	void UdpServer::do_write()
	{
		/*
		socket_.async_send_to(
			asio::buffer(*messages_.begin()), endpoint,
			[this](const asio::error_code& error, std::size_t bytes_transferred)
		{
			messages_.pop_front();
			if (!messages_.empty())
			{
				do_write();
			}
		}
		);
		*/
	}

	int UdpServer::send_to(const char* data, std::size_t size,
		const std::string& address, uint16_t port)
	{
		asio::ip::udp::endpoint endpoint =
			{ asio::ip::address::from_string(address), port };

		return send_to({ data, size }, endpoint);
	}

	int UdpServer::send_to(
		const std::string& data, asio::ip::udp::endpoint& endpoint)
	{
		return socket_.send_to(asio::buffer(data), endpoint);
	}

	int UdpServer::async_send_to(const char* data, std::size_t size,
		const std::string& address, uint16_t port)
	{
		asio::ip::udp::endpoint endpoint =
			{ asio::ip::address::from_string(address), port };

		return async_send_to({data, size}, endpoint);
	}

	int UdpServer::async_send_to(
		const std::string& data, asio::ip::udp::endpoint& endpoint)
	{
		messages_.push_back(data);

		socket_.async_send_to(
			asio::buffer(*messages_.begin()), endpoint,
			[this](const asio::error_code& error, std::size_t bytes_transferred)
			{
				messages_.pop_front();
				if (!messages_.empty())
				{
					do_write();
				}
			}
		);

		return 0;
	}
}
