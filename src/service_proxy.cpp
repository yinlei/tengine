#include "service_proxy.hpp"

#include "service.hpp"
#include "message.hpp"

namespace tengine
{
	ServiceProxy::ServiceProxy(Service *s)
		: host_(s)
	{

	}

	ServiceProxy::~ServiceProxy()
	{

	}

}

