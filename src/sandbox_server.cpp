#include "sandbox.hpp"

#include "context.hpp"
#include "server.hpp"

using namespace tengine;

struct server
{
	TcpServer *imp;
	int on_accept_ref;
	int on_read_ref;
	int on_closed_ref;
};

static int server_send_to_session(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct server *s = (struct server *)lua_touserdata(L, 1);
	if (!s)
	{
		return luaL_error(L, "please new channel first ...");
	}

	if (!s->imp)
		return luaL_error(L, "please new channel first ...");

	int session = (int)luaL_checkinteger(L, 2);

	size_t len;

	const char * data = luaL_checklstring(L, 3, &len);

	s->imp->send(session, data, len);

	lua_pushinteger(L, len);

	return 1;
}

static int server_close_session(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct server *s = (struct server *)lua_touserdata(L, 1);
	if (!s)
	{
		return luaL_error(L, "please new channel first ...");
	}

	if (!s->imp)
		return luaL_error(L, "please new channel first ...");

	int session = (int)luaL_checkinteger(L, 2);

	s->imp->close_session(session);

	return 0;
}

static int server_local_address(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct server *s = (struct server *)lua_touserdata(L, 1);
	if (!s)
	{
		return luaL_error(L, "please new channel first ...");
	}

	if (!s->imp)
		return luaL_error(L, "please new channel first ...");

	std::string address = s->imp->local_address();

	lua_pushstring(L, address.c_str());

	return 1;
}

static int server_remote_address(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct server *s = (struct server *)lua_touserdata(L, 1);
	if (!s || !s->imp)
	{
		return luaL_error(L, "please new channel first ...");
	}

	int session = (int)luaL_checkinteger(L, 2);

	std::string address = s->imp->address(session);

	lua_pushstring(L, address.c_str());

	return 1;
}

static int server_release(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct server *s = (struct server *)lua_touserdata(L, 1);

	if (s)
	{
		luaL_unref(L, LUA_REGISTRYINDEX, s->on_accept_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, s->on_read_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, s->on_closed_ref);

		if (s->imp)
		{
			delete s->imp;
			s->imp = nullptr;
		}
	}

	return 0;
}

static int server(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	int port = (int)luaL_checknumber(L, 1);

	luaL_checktype(L, 2, LUA_TTABLE);
	//lua_settop(L, 2);

	lua_getfield(L, 2, "on_accept");
	int accpet_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	lua_getfield(L, 2, "on_read");
	int read_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	lua_getfield(L, 2, "on_closed");
	int closed_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	TcpServer *server = new TcpServer(self, port);
	if (server == NULL)
		return luaL_error(L, "create server failed");

	struct server *s = (struct server*)lua_newuserdata(L, sizeof(*s));
	s->imp = server;
	s->on_accept_ref = accpet_handler;
	s->on_read_ref = read_handler;
	s->on_closed_ref = closed_handler;

	if (luaL_newmetatable(L, "server")) {
		luaL_Reg l[] = {
			{ "send", server_send_to_session },
			{ "close", server_close_session },
			{ "localaddress", server_local_address },
			{ "remoteaddress", server_remote_address },
			{ "__gc", server_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, server_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &TcpServer::TCPSERVER_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &TcpServer::TCPSERVER_KEY);
		lua_pushstring(L, "kv");
		lua_setfield(L, -2, "__mode");
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
	}

	lua_pushlightuserdata(L, s->imp);
	lua_pushvalue(L, -3);

	lua_settable(L, -3);

	lua_pop(L, 1);

	//lua_rawsetp(L, LUA_REGISTRYINDEX, server);
	//lua_rawgetp(L, LUA_REGISTRYINDEX, server);

	return 1;
}

void SandBox::server_accept(void* sender, int session)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);

	lua_rawgetp(L, LUA_REGISTRYINDEX, &TcpServer::TCPSERVER_KEY);

	lua_rawgetp(L, -1, sender);

	struct server* s = (struct server*)lua_touserdata(L, -1);
	if (s != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->on_accept_ref);
		lua_pushinteger(L, session);
		call(1, true);
	}

	lua_pop(L, 1);
	lua_pop(L, 1);
}

void SandBox::server_read(void* sender, int session, const char* data, std::size_t size)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);
	lua_rawgetp(L, LUA_REGISTRYINDEX, &TcpServer::TCPSERVER_KEY);

	lua_rawgetp(L, -1, sender);

	struct server* s = (struct server*)lua_touserdata(L, -1);
	if (s != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->on_read_ref);
		lua_pushinteger(L, session);
		lua_pushlstring(L, data, size);
		lua_pushinteger(L, size);
		call(3, true);
	}
	lua_pop(L, 1);
	lua_pop(L, 1);

	ccfree((void*)data);
}

void SandBox::server_closed(void* sender, int session, const char* error)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);
	lua_rawgetp(L, LUA_REGISTRYINDEX, &TcpServer::TCPSERVER_KEY);

	lua_rawgetp(L, -1, sender);

	struct server* s = (struct server*)lua_touserdata(L, -1);
	if (s != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->on_closed_ref);
		lua_pushinteger(L, session);
		lua_pushstring(L, error);
		call(2, true);
	}

	lua_pop(L, 1);
	lua_pop(L, 1);

	ccfree((void*)error);
}
