
#include "redis.hpp"

using namespace tengine;

struct redis
{
	Redis *imp;
	SandBox *self;
};

static int push_reply(lua_State *L, redisReply *reply)
{
	switch (reply->type) {
		case REDIS_REPLY_STRING:
		case REDIS_REPLY_STATUS: {
			lua_pushlstring(L, reply->str, reply->len);
		}
		break;
		case REDIS_REPLY_ARRAY: {
			lua_createtable(L, reply->elements, 0);
			for (size_t i = 0; i < reply->elements; i++) {
				push_reply(L, reply->element[i]);
				lua_rawseti(L, -2, i + 1);
			}
		}
		break;
		case REDIS_REPLY_INTEGER: {
			lua_pushinteger(L, reply->integer);
		}
		break;
		case REDIS_REPLY_NIL: {
			lua_pushnil(L);
		}
		break;
		case REDIS_REPLY_ERROR: {
			lua_pushlstring(L, reply->str, reply->len);
		}
		break;
	}
	return 1;
}

static int load_args(lua_State *L, int idx,
	const char** argv, std::size_t* argvlen)
{
	int nargs = lua_gettop(L) - idx + 1;

	if (nargs <= 0)
		return luaL_error(L, "missing command name");

	if (nargs > Redis::kRedisCommandMaxArgs)
		return luaL_error(L, "too many arguments");

	for (int i = 0; i < nargs; i++)
	{
		std::size_t len;
		const char* str = lua_tolstring(L, idx + i, &len);
		if (str == NULL)
			return luaL_argerror(L, idx + i,
				"expected a string or number value");

		argv[i] = str;
		argvlen[i] = len;
	}

	return nargs;
}

static int redis_call(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct redis *my = (struct redis*)lua_touserdata(L, 1);
	if (!my || !my->imp)
	{
		return luaL_error(L, "please new redis first ...");
	}

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushvalue(L, 2);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);

	size_t len;
	const char * data = luaL_checklstring(L, 3, &len);

	my->imp->call(data, len,
		[=](redisContext *context, redisReply *reply)
	{
		if (connect == nullptr || reply == nullptr)
			return;

		lua_State* L = my->self->state();

		lua_rawgeti(L, LUA_REGISTRYINDEX, callback);

		if (reply->type == REDIS_REPLY_ERROR)
			lua_pushboolean(L, 0);
		else
			lua_pushboolean(L, 1);

		push_reply(L, reply);

		my->self->call(2, true);

		luaL_unref(L, LUA_REGISTRYINDEX, callback);
	});

	return 0;
}

static int redis_callv(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct redis *my = (struct redis*)lua_touserdata(L, 1);
	if (!my || !my->imp)
	{
		return luaL_error(L, "please new redis first ...");
	}

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushvalue(L, 2);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);

	const char* argv[Redis::kRedisCommandMaxArgs];
	std::size_t argvlen[Redis::kRedisCommandMaxArgs];

	int nargs = load_args(L, 3, argv, argvlen);

	my->imp->call(argv, argvlen, nargs,
		[=](redisContext *context, redisReply *reply)
	{
		if (reply == nullptr || context == nullptr)
			return;

		lua_State* L = my->self->state();

		lua_rawgeti(L, LUA_REGISTRYINDEX, callback);

		if (reply->type == REDIS_REPLY_ERROR)
			lua_pushboolean(L, 0);
		else
			lua_pushboolean(L, 1);

		push_reply(L, reply);

		my->self->call(2, true);

		luaL_unref(L, LUA_REGISTRYINDEX, callback);
	});

	return 0;
}

static int redis_pipeline(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct redis *my = (struct redis*)lua_touserdata(L, 1);
	if (!my || !my->imp)
	{
		return luaL_error(L, "please new redis first ...");
	}

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushvalue(L, 2);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushnil(L);
	while(lua_next(L, 3) != 0) {
		const char* argv[Redis::kRedisCommandMaxArgs];
		std::size_t argvlen[Redis::kRedisCommandMaxArgs];

		int nargs = load_args(L, 3, argv, argvlen);

		my->imp->pipeline(argv, argvlen, nargs);

		lua_pop(L, 1);
	}

	return 0;
}

static int redis_commit(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct redis *my = (struct redis*)lua_touserdata(L, 1);
	if (!my || !my->imp)
        return luaL_error(L, "please new redis first ...");

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushvalue(L, 2);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);

	my->imp->transaction();

	lua_pushnil(L);
	while(lua_next(L, 3) != 0) {
		const char* argv[Redis::kRedisCommandMaxArgs];
		std::size_t argvlen[Redis::kRedisCommandMaxArgs];

		std::size_t nargs = 0;
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			if (nargs >= Redis::kRedisCommandMaxArgs)
				return luaL_error(L, "too many arguments");

			std::size_t len;
			const char* str = lua_tolstring(L, -1, &len);
			if (str == NULL)
				return luaL_argerror(L, -1,
					"expected a string or number value");

			argv[nargs] = str;
			argvlen[nargs] = len;
			nargs++;

			lua_pop(L, 1);
		}

		my->imp->pipeline(argv, argvlen, nargs);

		lua_pop(L, 1);
	}

	my->imp->commit(
		[=](redisContext *context, redisReply **replys, std::size_t size)
		{
			lua_State* L = my->self->state();

			lua_rawgeti(L, LUA_REGISTRYINDEX, callback);
			lua_pushboolean(L, 1);
			lua_newtable(L);

			for (std::size_t i = 0; i < size; i++)
			{
				lua_newtable(L);
				redisReply *reply = replys[i];

				if (reply->type == REDIS_REPLY_ERROR)
					lua_pushboolean(L, 0);
				else
					lua_pushboolean(L, 1);
				lua_seti(L, -2, 1);

				push_reply(L, reply);
				lua_seti(L, -2, 2);

				lua_seti(L, -2, i+1);
			}
			
			my->self->call(2, true);

			luaL_unref(L, LUA_REGISTRYINDEX, callback);
		});

	return 0;
}

static int redis_release(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct mysql *my = (struct mysql*)lua_touserdata(L, 1);

	if (my)
	{
		if (my->imp)
		{
			delete my->imp;
			my->imp = nullptr;
		}
	}

	return 1;
}

static int redis(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	const char *conf = luaL_checkstring(L, 1);

	Redis *redis = new Redis(self);
	if (redis == nullptr)
		return luaL_error(L, "create redis failed !!!");

	if (redis->start(conf) != 0)
		return luaL_error(L, "start redis failed !!!");

	struct redis *my = (struct redis*)lua_newuserdata(L, sizeof(*my));
	my->imp = redis;
	my->self = self;

	if (luaL_newmetatable(L, "redis")) {
		luaL_Reg l[] = {
			{ "call", redis_call },
			{ "callv", redis_callv },
			{ "pipeline", redis_pipeline },
			{ "commit", redis_commit },
			{ "__gc", redis_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, redis_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &Redis::REDIS_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &Redis::REDIS_KEY);
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
