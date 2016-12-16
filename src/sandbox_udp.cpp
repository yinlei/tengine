#include "sandbox.hpp"

#include "context.hpp"
#include "server.hpp"

using namespace tengine;

struct udp_server
{
	UdpServer *imp;
	int on_read_ref;
};

static int udp_server_async_send_to(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_server *s = (struct udp_server *)lua_touserdata(L, 1);
	if (!s)
    {
        return luaL_error(L, "please new channel first ...");
    }

	if (!s->imp)
		return luaL_error(L, "please new channel first ...");

	size_t len;

	const char * data = luaL_checklstring(L, 2, &len);

	const char * address = luaL_checkstring(L, 3);

	int port = (int)luaL_checkinteger(L, 4);

	s->imp->async_send_to(data, len, address, port);

	return 0;
}

static int udp_server_send_to(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_server *s = (struct udp_server *)lua_touserdata(L, 1);
	if (!s)
	{
		return luaL_error(L, "please new channel first ...");
	}

	if (!s->imp)
		return luaL_error(L, "please new channel first ...");

	size_t len;

	const char * data = luaL_checklstring(L, 2, &len);

	const char * address = luaL_checkstring(L, 3);

	int port = (int)luaL_checkinteger(L, 4);

	s->imp->send_to(data, len, address, port);

	return 0;
}

static int udp_server_join_group(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_server *s = (struct udp_server *)lua_touserdata(L, 1);
	if (!s)
		return luaL_error(L, "please new channel first ...");

	if (!s->imp)
		return luaL_error(L, "please new channel first ...");

	size_t len;

	const char * multicast_address = luaL_checklstring(L, 2, &len);

	if (!multicast_address)
		return luaL_error(L, "please new channel first ...");

	s->imp->join_group(multicast_address);

	return 0;
}

static int udp_server_release(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_server *s = (struct udp_server *)lua_touserdata(L, 1);

	if (s)
	{
		luaL_unref(L, LUA_REGISTRYINDEX, s->on_read_ref);

		if (s->imp)
		{
			delete s->imp;
			s->imp = nullptr;
		}
	}

	return 1;
}

static int udp_server(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	int port = (int)luaL_checknumber(L, 1);

	luaL_checktype(L, 2, LUA_TTABLE);
	//lua_settop(L, 2);

	lua_getfield(L, 2, "on_read");
	int read_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	UdpServer *server = new UdpServer(self, port);
	if (server == NULL)
		return luaL_error(L, "create udp_server failed");

	server->start();

	struct udp_server *s = (struct udp_server*)lua_newuserdata(L, sizeof(*s));
	s->imp = server;
	s->on_read_ref = read_handler;

	if (luaL_newmetatable(L, "udp_server")) {
		luaL_Reg l[] = {
			{ "asendto", udp_server_async_send_to },
			{ "sendto", udp_server_send_to },
			{ "joingroup", udp_server_join_group },
			{ "__gc", udp_server_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, udp_server_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &UdpServer::UDPSERVER_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &UdpServer::UDPSERVER_KEY);
		lua_pushstring(L, "kv");
		lua_setfield(L, -2, "__mode");
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
	}

	lua_pushlightuserdata(L, s->imp);
	lua_pushvalue(L, -3);

	lua_settable(L, -3);

	lua_pop(L, 1);

	return 1;
}

void SandBox::server_udp_read(void* sender, const std::string& address, uint16_t port, const char* data, std::size_t size)
{
	lua_State *L = l_;

	lua_rawgetp(L, LUA_REGISTRYINDEX, &UdpServer::UDPSERVER_KEY);

	lua_rawgetp(L, -1, sender);

	struct udp_server* s = (struct udp_server*)lua_touserdata(L, -1);
	if (s != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->on_read_ref);
		lua_pushlstring(L, data, size);
		lua_pushlstring(L, address.c_str(), address.size());
		lua_pushinteger(L, port);
		call(3, true);
	}

	ccfree((void*)data);
}