#ifndef TENGINE_SERVICE_PROXY_HPP
#define TENGINE_SERVICE_PROXY_HPP

#include "allocator.hpp"

namespace tengine
{
	class Service;

	class ServiceProxy : public Allocator
	{
	public:
		ServiceProxy(Service *s);

		virtual ~ServiceProxy();

		virtual int start() { return 0; }

		Service *host() { return host_; }

	protected:

		Service *host_;
	};

}

#endif
