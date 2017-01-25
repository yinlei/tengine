#ifndef TENGINE_SANDBOX_HPP
#define TENGINE_SANDBOX_HPP

#include "service.hpp"
#include "channel.hpp"

#include <map>

#include <experimental/filesystem>
#ifdef _WIN32
namespace fs = std::tr2::sys;
#else
namespace fs = std::experimental::filesystem;
#endif

struct lua_State;

namespace tengine
{
	class Context;

	class SandBox : public Service
	{
	public:
		SandBox() = delete;

		SandBox(const SandBox&) = delete;

		SandBox& operator=(const SandBox&) = delete;

		SandBox(Context& context);

		virtual ~SandBox();

		virtual int init(const char* name);

		lua_State* state();

		int call(int num, bool remove = true, lua_State* L = nullptr);

	public:

		template<int MessageType, class... Args>
		void handler(MessageTypeTrait<MessageType>, int src, Args... args);

		void handler(int src, const fs::path &path, void* data);

		void handler(int src, const std::vector<fs::path> &paths, void* data);

	private:

		int inject(const char *name);

		void timer(void* timer_id, int session);

		void server_accept(void* sender, int session);

		void server_read(
			void* sender, int session, const char* data, std::size_t size);

		void server_closed(void* sender, int session, const char* error);

		void server_udp_read(void *sender, const std::string& address,
			uint16_t port, const char* data, std::size_t size);

		void channel_connected(void *sender);

		void channel_read(void *sender, const char* data, std::size_t size);

		void channel_closed(void *sender, const char* error);

		void udp_channel_read(void *sender, const std::string& address,
			uint16_t port, const char* data, std::size_t size);

		void udp_sender_read(void *sender, const std::string& address,
			uint16_t port, const char* data, std::size_t size);

		void dispatch(int type, int src, int session, const char* data, std::size_t size);

		lua_State* l_;

		Service *timer_;

		Service *logger_;

	public:
		typedef std::map<void*, ChannelPtr> ChannelPtrMap;

		ChannelPtrMap channels;

		typedef std::map<void*, UdpChannelPtr> UdpChannelPtrMap;

		UdpChannelPtrMap udp_channels;

		typedef std::map<void*, UdpSenderPtr> UdpSenderPtrMap;

		UdpSenderPtrMap udp_senders;
	};

	// timer
	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageTimer>,
		int src, void* timer_id, int session)
	{
		timer(timer_id, session);
	}

	// server
	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageTcpServerAccept>, int src,
		void* sender, int session)
	{
		server_accept(sender, session);
	}

	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageTcpServerRead>, int src,
		void* sender, int session, const char* data, std::size_t size)
	{
		server_read(sender, session, data, size);
	}

	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageTcpServerClosed>, int src,
		void* sender, int session, const char* error)
	{
		server_closed(sender, session, error);
	}

	// channel
	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageChannelConnected>, int src,
		void* sender)
	{
		channel_connected(sender);
	}

	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageChannelRead>, int src,
		void* sender, const char* data, std::size_t size)
	{
		channel_read(sender, data, size);
	}

	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageChannelClosed>, int src,
		void* sender, const char* error)
	{
		channel_closed(sender, error);
	}

	// udp
	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageUdpServerRead>, int src,
		void* sender, std::string address, uint16_t port, const char* data,
		std::size_t size)
	{
		server_udp_read(sender, address, port, data, size);
	}

	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageUdpChannelRead>, int src,
		void* sender, std::string address, uint16_t port, const char* data,
		std::size_t size)
	{
		udp_channel_read(sender, address, port, data, size);
	}

	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageUdpSenderRead>, int src,
		void* sender, std::string address, uint16_t port, const char* data,
		std::size_t size)
	{
		udp_sender_read(sender, address, port, data, size);
	}

	// rpc
	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageServiceRequest>,
		int src, int session, const char* data, std::size_t size)
	{
		dispatch((int)MessageType::kMessageServiceRequest, src, session, data, size);
	}

	template<>
	inline void SandBox::handler(MessageTypeTrait<MessageType::kMessageServiceResponse>,
		int src, int session, const char* data, std::size_t size)
	{
		dispatch((int)MessageType::kMessageServiceResponse, src, session, data, size);
	}

}

#endif
