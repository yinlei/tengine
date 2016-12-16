#ifndef TENGINE_NODE_HPP
#define TENGINE_NODE_HPP

#include "asio.hpp"

#include "service.hpp"
#include "process_info.hpp"

#include <stdint.h>
#include <thread>
#include <set>
#include <map>

namespace tengine
{
	class Announcer;

	class Discoverer;

	class Node : public Service
	{
	public:
		Node(Context& context);

		virtual ~Node();

		virtual int init(const char* name);

		int announce(const std::string& name, int id);

		template<class Handler>
		int discovery(const std::string& name, Handler handler)
		{
			return 0;
		}

		static std::string name()
		{
			std::ostringstream os;
			os << asio::ip::host_name() << ":" << ProcessInfo::current().id();
			return os.str();
		}

	private:
		std::string node_name_;

		asio::io_service io_service_;

		asio::io_service::work work_;

		std::thread thread_;

		Discoverer *discover_;

		typedef std::map<int, Discoverer*> DiscovererMap;

		DiscovererMap discovers_;

		typedef std::map<int, Announcer*> AnnouncerMap;

		AnnouncerMap anncouncers_;

	};
}

#endif // ! TENGINE_NODE_HPP
