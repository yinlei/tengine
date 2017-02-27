#include "sandbox.hpp"

#include "context.hpp"
#include "network.hpp"

using namespace tengine;

static int http(lua_State* L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	Network *net_work = (Network*)context->query("Network");

	if (net_work == nullptr)
		return luaL_error(L, "no Network service");

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	luaL_checktype(L, 1, LUA_TSTRING);
	const char *type = lua_tostring(L, 1);

    luaL_checktype(L, 2, LUA_TSTRING);
    const char *url = lua_tostring(L, 2);

    luaL_checktype(L, 3, LUA_TSTRING);
    const char *path = lua_tostring(L, 3);

	const char *content = "";

    if (!lua_isnoneornil(L, 4))
    {
        luaL_checktype(L, 4, LUA_TSTRING);
		content = lua_tostring(L, 4);
    }

	luaL_checktype(L, 5, LUA_TFUNCTION);
	lua_pushvalue(L, 5);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);

    net_work->async_request(type, url, path, content,
		[=](std::string r) {
		asio::post(self->executor(),
			[=]
			{
				lua_rawgeti(L, LUA_REGISTRYINDEX, callback);
				lua_pushlstring(L, r.c_str(), r.size());
				self->call(1, true);
			});
	});

	//lua_pushstring(L, r.c_str());
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

struct web
{
	SandBox *self;
	WebServer* imp;
	int callback;
};

static int web_start(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct web *my = (struct web*)lua_touserdata(L, 1);
	if (!my || !my->imp)
	{
		return luaL_error(L, "please new web first ...");
	}

	int port = (int)luaL_checknumber(L, 2);

	luaL_checktype(L, 3, LUA_TFUNCTION);
	lua_pushvalue(L, 3);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);

	my->callback = callback;

	my->imp->start(port,
		[=](void *res, const char* type, const char* path, const char* content)
	{
		lua_State* L = my->self->state();

		lua_rawgeti(L, LUA_REGISTRYINDEX, callback);

		lua_pushlightuserdata(L, res);

		lua_pushstring(L, type);

		lua_pushstring(L, path);

		lua_pushstring(L, content);

		return my->self->call(4, false);
	});

	return 1;
}

static int web_response(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct web *my = (struct web*)lua_touserdata(L, 1);
	if (!my || !my->imp)
	{
		return luaL_error(L, "please new web first ...");
	}

	void* response = lua_touserdata(L, 2);

	size_t len;
	const char * data = luaL_checklstring(L, 3, &len);

	my->imp->response(response, data, len);

	return 0;
}

static int web_release(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct web *my = (struct web*)lua_touserdata(L, 1);
	if (!my)
	{
		return luaL_error(L, "type is not web!!!");
	}

	luaL_unref(L, LUA_REGISTRYINDEX, my->callback);

	lua_rawgetp(L, LUA_REGISTRYINDEX, &WebServer::WEBSERVER_KEY);

	lua_pushlightuserdata(L, my->imp);
	lua_pushnil(L);
	lua_rawset(L, -3);

	if (my->imp) {
		delete my->imp;
		my->imp = 0;
	}

	my->self = 0;
	my->callback = 0;

	lua_settop(L, 0);

	return 1;
}

static int web(lua_State* L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	WebServer *web_server = new WebServer(self);

	if (web_server == nullptr)
		return luaL_error(L, "create web server failed!!!");

	struct web *my = (struct web*)lua_newuserdata(L, sizeof(*my));
	my->imp = web_server;
	my->self = self;
	my->callback = 0;

	if (luaL_newmetatable(L, "web")) {
		luaL_Reg l[] = {
			{ "start", web_start },
			{ "response", web_response },
			{ "__gc", web_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, web_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &WebServer::WEBSERVER_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &WebServer::WEBSERVER_KEY);
		lua_pushstring(L, "kv");
		lua_setfield(L, -2, "__mode");
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
	}

	lua_pushlightuserdata(L, my->imp);
	lua_pushvalue(L, -3);

	lua_settable(L, -3);

	lua_pop(L, 1);

	return 1;
}


///////////////////////////////////////////////////////////////////////////////
struct webserver
{
	SandBox *self;
	WebSocket* imp;
	int on_open_ref;
	int on_message_ref;
	int on_close_ref;
	int on_error_ref;
};

static int webserver_start(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct webserver *my = (struct webserver*)lua_touserdata(L, 1);
	if (!my || !my->imp)
	{
		return luaL_error(L, "please new webserver first ...");
	}

	const char* path = luaL_checkstring(L, 2);

	luaL_checktype(L, 3, LUA_TTABLE);
	//lua_settop(L, 2);

	lua_getfield(L, 3, "on_open");
	my->on_open_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	lua_getfield(L, 3, "on_message");
	my->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	lua_getfield(L, 3, "on_close");
	my->on_close_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_getfield(L, 3, "on_error");
	my->on_error_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	my->imp->start(path);

	return 1;
}

static int webserver_send(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct webserver *my = (struct webserver *)lua_touserdata(L, 1);
	if (!my)
		return luaL_error(L, "type is not webserver!!!");

	if (!my->imp)
		return luaL_error(L, "type is not webserver!!!");

	int session = (int)luaL_checkinteger(L, 2);

	size_t len;

	const char * data = luaL_checklstring(L, 3, &len);

	my->imp->send(session, data, len);

	lua_pushinteger(L, len);

	return 1;
}

static int webserver_release(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct webserver *my = (struct webserver*)lua_touserdata(L, 1);
	if (!my)
	{
		return luaL_error(L, "type is not webserver!!!");
	}

	luaL_unref(L, LUA_REGISTRYINDEX, my->on_open_ref);
	luaL_unref(L, LUA_REGISTRYINDEX, my->on_message_ref);
	luaL_unref(L, LUA_REGISTRYINDEX, my->on_close_ref);
	luaL_unref(L, LUA_REGISTRYINDEX, my->on_error_ref);

	lua_rawgetp(L, LUA_REGISTRYINDEX, &WebSocket::WEBSOCKET_KEY);

	lua_pushlightuserdata(L, my->imp);
	lua_pushnil(L);
	lua_rawset(L, -3);

	if (my->imp) {
		delete my->imp;
		my->imp = 0;
	}

	my->self = 0;
	my->on_open_ref = 0;
	my->on_message_ref = 0;
	my->on_close_ref = 0;
	my->on_error_ref = 0;

	lua_settop(L, 0);

	return 1;
}

static int webserver(lua_State* L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	int port = (int)luaL_checknumber(L, 1);

	WebSocket *web_socket = new WebSocket(self, port);

	if (web_socket == nullptr)
		return luaL_error(L, "create web server failed!!!");

	struct webserver *my = (struct webserver*)lua_newuserdata(L, sizeof(*my));
	my->imp = web_socket;
	my->self = self;
	my->on_open_ref = 0;
	my->on_message_ref = 0;
	my->on_close_ref = 0;
	my->on_error_ref = 0;

	if (luaL_newmetatable(L, "webserver")) {
		luaL_Reg l[] = {
			{ "start", webserver_start },
			{ "send", webserver_send },
			{ "__gc", webserver_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, webserver_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &WebSocket::WEBSOCKET_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &WebSocket::WEBSOCKET_KEY);
		lua_pushstring(L, "kv");
		lua_setfield(L, -2, "__mode");
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
	}

	lua_pushlightuserdata(L, my->imp);
	lua_pushvalue(L, -3);

	lua_settable(L, -3);

	lua_pop(L, 1);

	return 1;
}

void SandBox::webserver_open(void* sender, int session)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);

	lua_rawgetp(L, LUA_REGISTRYINDEX, &WebSocket::WEBSOCKET_KEY);

	lua_rawgetp(L, -1, sender);

	struct webserver* s = (struct webserver*)lua_touserdata(L, -1);
	if (s != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->on_open_ref);
		lua_pushinteger(L, session);
		call(1, true);
	}

	lua_pop(L, 1);
	lua_pop(L, 1);
}

void SandBox::webserver_message(void* sender, int session, const char* data, std::size_t size)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);
	lua_rawgetp(L, LUA_REGISTRYINDEX, &WebSocket::WEBSOCKET_KEY);

	lua_rawgetp(L, -1, sender);

	struct webserver* s = (struct webserver*)lua_touserdata(L, -1);
	if (s != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->on_message_ref);
		lua_pushinteger(L, session);
		lua_pushlstring(L, data, size);
		lua_pushinteger(L, size);
		call(3, true);
	}
	lua_pop(L, 1);
	lua_pop(L, 1);

	ccfree((void*)data);
}

void SandBox::webserver_close(void* sender, int session, int status, const std::string& reason)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);
	lua_rawgetp(L, LUA_REGISTRYINDEX, &WebSocket::WEBSOCKET_KEY);

	lua_rawgetp(L, -1, sender);

	struct webserver* s = (struct webserver*)lua_touserdata(L, -1);
	if (s != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->on_close_ref);
		lua_pushinteger(L, session);
		lua_pushinteger(L, status);
		lua_pushlstring(L, reason.c_str(), reason.size());
		call(3, true);
	}

	lua_pop(L, 1);
	lua_pop(L, 1);
}


void SandBox::webserver_error(void* sender, int session, const std::string& error)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);
	lua_rawgetp(L, LUA_REGISTRYINDEX, &WebSocket::WEBSOCKET_KEY);

	lua_rawgetp(L, -1, sender);

	struct webserver* s = (struct webserver*)lua_touserdata(L, -1);
	if (s != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->on_error_ref);
		lua_pushinteger(L, session);
		lua_pushlstring(L, error.c_str(), error.size());
		call(2, true);
	}

	lua_pop(L, 1);
	lua_pop(L, 1);
}