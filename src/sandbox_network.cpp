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
		[=](const char* type, const char* path, const char* content)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, callback);

		lua_pushstring(L, type);

		lua_pushstring(L, path);

		lua_pushstring(L, content);

		int ret = my->self->call(3, false);

		return lua_tostring(L, -1);
	});

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

	return 0;
}
