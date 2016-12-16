#include "sandbox.hpp"

#include "context.hpp"
#include "watchdog.hpp"

using namespace tengine;

struct watchdog
{
	WatchDog *imp;
	void *w;
	SandBox *self;
	int callback;
};

static int watchdog_watch(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct watchdog *s = (struct watchdog *)lua_touserdata(L, 1);
	if (!s)
	{
		return luaL_error(L, "please new watchdog first ...");
	}

	if (!s->imp)
		return luaL_error(L, "please new watchdog first ...");

	return 0;
}

static int watchdog_unwatch(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct watchdog *s = (struct watchdog *)lua_touserdata(L, 1);
	if (!s)
	{
		return luaL_error(L, "please new watchdog first ...");
	}

	if (!s->imp)
		return luaL_error(L, "please new watchdog first ...");

	return 0;
}

static int watchdog_release(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct watchdog *s = (struct watchdog *)lua_touserdata(L, 1);

	if (s)
	{
		luaL_unref(L, LUA_REGISTRYINDEX, s->callback);

		if (s->imp)
		{
			//s->imp->unwatch();
		}
	}

	return 1;
}

static int watchdog(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	WatchDog *watch_dog = (WatchDog*)context->query("WatchDog");

	if (watch_dog == nullptr)
		return luaL_error(L, "no WatchDog service");

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	size_t len;

	const char * path = luaL_checklstring(L, 1, &len);

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushvalue(L, 2);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);

	void *w = watch_dog->watch(self, path);

	struct watchdog *my = (struct watchdog*)lua_newuserdata(L, sizeof(*my));
	my->imp = watch_dog;
	my->w = w;
	my->self = self;
	my->callback = callback;

	if (luaL_newmetatable(L, "watchdog")) {
		luaL_Reg l[] = {
			{ "watch", watchdog_watch },
			{ "close", watchdog_unwatch },
			{ "__gc", watchdog_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, watchdog_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &WatchDog::WATCHDOG_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &WatchDog::WATCHDOG_KEY);
		lua_pushstring(L, "kv");
		lua_setfield(L, -2, "__mode");
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
	}

	lua_pushlightuserdata(L, my->w);
	lua_pushvalue(L, -3);

	lua_settable(L, -3);

	lua_pop(L, 1);

	return 1;
}
