#include "sandbox.hpp"

#include "context.hpp"
#include "network.hpp"
#include "node.hpp"
#include "system_info.hpp"
#include "dispatch.hpp"

#include <experimental/filesystem>
#ifdef _WIN32
namespace fs = std::tr2::sys;
#else
namespace fs = std::experimental::filesystem;
#endif

using namespace tengine;

static int start(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	const char *name = lua_tostring(L, 1);

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	if (name == NULL)
		return luaL_error(L, "service name ...");

	SandBox *sandbox = context->LaunchSandBox(name);
	if (sandbox == NULL)
		return luaL_error(L, "service init failed!");

	lua_pushlightuserdata(L, sandbox);
	lua_pushinteger(L, sandbox->id());

	return 2;
}

static int query(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	const char *name = lua_tostring(L, 1);

	if (name == NULL)
		return luaL_error(L, "need service name");

	Service *s = context->query(name);

	if (s != NULL)
	{
		lua_pushlightuserdata(L, s);
		return 1;
	}

	return 0;
}

static int register_name(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	const char *name = lua_tostring(L, 1);

	if (name == NULL)
		return luaL_error(L, "need service name");

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	context->register_name(self, name);

	return 0;
}

static int send(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	Service *dest = NULL;

	int type = lua_type(L, 1);

	switch (lua_type(L, 1))
	{
	case LUA_TNUMBER:
	{
		int service_id = (int)luaL_checknumber(L, 1);

		dest = context->query(service_id);
	}
	break;
	case LUA_TSTRING:
	{
		const char* service_name = luaL_checkstring(L, 1);
		if (service_name == NULL)
			return luaL_error(L, "no dest service");

		dest = context->query(service_name);
	}
	break;
	case LUA_TLIGHTUSERDATA:
	{
		dest = (Service*)lua_touserdata(L, 1);
	}
	break;
	default:
		luaL_error(L, "send param error");
	}

	if (!dest)
		luaL_error(L, "dest is null");

	int session = (int)luaL_checknumber(L, 2);

	std::size_t len;
	void* data;
	//const char* data = luaL_checkstring(L, 3);

	switch (lua_type(L, 3))
	{
	case LUA_TSTRING:
	{
		const char* tmp = lua_tolstring(L, 3, &len);

		data = ccmalloc(len);
		memcpy(data, tmp, len);
	}
	break;
	case LUA_TLIGHTUSERDATA:
	{
		data = lua_touserdata(L, 3);
		len = (std::size_t)luaL_checknumber(L, 4);
	}
	break;
	default:
		luaL_error(L, "send param error");
	}

	switch (session)
	{
	case 0:
		// call
		session = self->session();
	case -1:
		// send
		dispatch<MessageType::kMessageServiceRequest, SandBox>(self, dest, session, (const char*)data, len);
		break;
	default:
		// return
		dispatch<MessageType::kMessageServiceResponse, SandBox>(self, dest, session, (const char*)data, len);
	}

	lua_pushinteger(L, session);

	return 1;
}

static int dispatch(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	luaL_checktype(L, 1, LUA_TFUNCTION);
	lua_settop(L, 1);
	lua_rawsetp(L, LUA_REGISTRYINDEX, self);
	return 1;
}

static int thread_id(lua_State* L)
{
	std::ostringstream ss;
	ss << std::this_thread::get_id();
	lua_pushlstring(L, ss.str().c_str(), ss.str().length());

	return 1;
}

static int now(lua_State* L)
{
	lua_pushinteger(L, Timer::now());
	return 1;
}

static int micro_now(lua_State* L)
{
	lua_pushinteger(L, Timer::micro_now());
	return 1;
}

static int nano_now(lua_State* L)
{
	lua_pushinteger(L, Timer::nano_now());
	return 1;
}

static int system_info(lua_State* L)
{
	lua_newtable(L);
	lua_pushinteger(L, SystemInfo::count_of_processors());
	lua_setfield(L, -2, "processors");

	lua_pushinteger(L, SystemInfo::amount_of_physical_memory());
	lua_setfield(L, -2, "memory");

	lua_pushfstring(L, SystemInfo::operating_system_name().c_str());
	lua_setfield(L, -2, "name");

	return 1;
}

static int process_info(lua_State* L)
{
	ProcessInfo info = ProcessInfo::current();

	lua_newtable(L);
	lua_pushinteger(L, info.id());
	lua_setfield(L, -2, "id");

	lua_pushinteger(L, info.amount_of_thread());
	lua_setfield(L, -2, "threads");

	lua_pushinteger(L, info.amount_of_memory_used_kb());
	lua_setfield(L, -2, "memory_used");

	lua_pushinteger(L, info.amount_of_vmemory_used_kb());
	lua_setfield(L, -2, "vmemory_used");

	lua_pushinteger(L, info.cpu_usage());
	lua_setfield(L, -2, "cpu_usage");

	return 1;
}

static int announcer(lua_State* L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	Node *node = (Node*)context->query("Node");
	if (node == nullptr)
		return luaL_error(L, "no node service");

	const char* name = lua_tostring(L, 1);
	int id = (int)luaL_checknumber(L, 2);

	node->announce(std::string(name), id);

	return 0;
}

static int files(lua_State* L)
{
	const char* path = luaL_checkstring(L, 1);
	if (!path)
	{
		luaL_error(L, "no path");
	}

	int recursive = 0;

	if (!lua_isnoneornil(L, 2))
	{
		recursive = 1;
	}

	//TODO filter

	lua_newtable(L);

	lua_Integer i = 1;

	if (recursive)
	{
		for (fs::path p : fs::recursive_directory_iterator(path))
		{
			lua_pushstring(L, p.filename().string().c_str());
			lua_seti(L, -2, i);

			i++;
		}
	}
	else
	{
		for (fs::path p : fs::directory_iterator(path))
		{
			lua_pushstring(L, p.stem().string().c_str());
			lua_seti(L, -2, i);

			i++;
		}
	}

    return 1;
}

void SandBox::dispatch(int type, int src, int session, const char* data, std::size_t size)
{
	lua_State *L = l_;
	lua_rawgetp(L, LUA_REGISTRYINDEX, this);

	if (!lua_isfunction(L, -1))
	{
		luaL_error(L, "callback is not function");
		ccfree((void*)data);
	}

	lua_pushinteger(L, type);
	lua_pushinteger(L, src);
	lua_pushlightuserdata(L, this);
	lua_pushinteger(L, session);
	lua_pushlstring(L, data, size);

	call(5, true);

	ccfree((void*)data);
}
