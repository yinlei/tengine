
#include "mysql.h"

struct mysql_wrap
{
	MYSQL* mysql;
	int closed;
};

static int mysql_connect(lua_State *L) {
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct mysql_wrap *m = (struct mysql_wrap *)lua_touserdata(L, 1);
	if (!m->mysql)
	{
		return luaL_error(L, "please new mysql first ...");
	}

	//	host user password
	const char *host = luaL_checkstring(L, 2);
	if (host == NULL)
		return luaL_error(L, "host is null");

	int port = (int)luaL_checknumber(L, 3);

	const char* user = luaL_checkstring(L, 4);
	if (user == NULL)
		return luaL_error(L, "user is null");

	const char* password = luaL_checkstring(L, 5);
	if (password == NULL)
		return luaL_error(L, "password is null");

	const char* db = luaL_checkstring(L, 6);
	if (db == NULL)
		return luaL_error(L, "db is null");

	char timeout = 10;
	mysql_options(m->mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

	if (0 != mysql_options(m->mysql, MYSQL_SET_CHARSET_NAME, "utf8"))
	{
		return luaL_error(L, mysql_error(m->mysql));
	}

	if (mysql_real_connect(m->mysql, host, user, password, db, port, NULL, 0) == NULL)
	{
		return luaL_error(L, mysql_error(m->mysql));
	}
	else
	{
		const char sql[] = "set interactive_timeout=24*3600";
		int ret = mysql_real_query(m->mysql, sql, (unsigned long)sizeof(sql));
		if (ret != 0)
		{
			return luaL_error(L, mysql_error(m->mysql));
		}
	}

	return 1;
}

static int mysql_query(lua_State* L) {
	luaL_checktype(L, 1, LUA_TUSERDATA);
	struct mysql_wrap *m = (struct mysql_wrap *)lua_touserdata(L, 1);
	if (!m)
	{
		lua_pushnil(L);
		return 1;
	}

	const char *sql = luaL_checkstring(L, 2);
	if (sql == NULL)
		return luaL_error(L, "sql is null");

	std::string sql_str = sql;

	int ret = mysql_real_query(m->mysql, sql_str.c_str(), sql_str.size());
	if (ret != 0)
	{
		lua_pushnil(L);
		lua_pushstring(L, mysql_error(m->mysql));
		return 2;
	}

	int field_count = mysql_field_count(m->mysql);

	switch (field_count)
	{
	case 0:
	{
		lua_newtable(L);
		lua_pushinteger(L, mysql_affected_rows(m->mysql));
		lua_setfield(L, -2, "affected_rows");

		lua_pushinteger(L, mysql_insert_id(m->mysql));
		lua_setfield(L, -2, "insert_id");

		lua_pushfstring(L, mysql_sqlstate(m->mysql));
		lua_setfield(L, -2, "server_status");

		lua_pushinteger(L, mysql_warning_count(m->mysql));
		lua_setfield(L, -2, "warning_count");

		return 1;
	}
	break;
	default:
	{
		MYSQL_RES *result = mysql_store_result(m->mysql);
		if (NULL != result)
		{
			int num_fields = mysql_num_fields(result);

			MYSQL_FIELD ** fds = (MYSQL_FIELD **)ccmalloc(sizeof(MYSQL_FIELD *)* num_fields);
			MYSQL_FIELD * fd;
			for (int i = 0; fd = mysql_fetch_field(result); ++i)
			{
				fds[i] = fd;
			}

			lua_newtable(L);

			MYSQL_ROW row;
			int index = 0;
			while ((row = mysql_fetch_row(result)))
			{
				unsigned long *lengths;
				lengths = mysql_fetch_lengths(result);

				lua_newtable(L);
				for (int i = 0; i < num_fields; i++)
				{
					char* s = row[i];
					if (IS_NUM(fds[i]->type))
					{
						lua_pushnumber(L, atol(s));
					}
					else
					{
						lua_pushlstring(L, s, lengths[i]);
					}
					//lua_seti(L, -2, (i + 1));
					lua_setfield(L, -2, fds[i]->name);
				}
				index++;
				lua_seti(L, -2, index);
			}

			ccfree(fds);
			mysql_free_result(result);
			return 1;
		}
	}
	}

	return 0;
}

static int mysql_close(lua_State* L) {
	struct mysql_wrap *m = (struct mysql_wrap *)lua_touserdata(L, 1);

	if (!m->closed)
		mysql_close(m->mysql);

	return 1;
}

static int mysql_release(lua_State* L) {
	struct mysql_wrap *m = (struct mysql_wrap *)lua_touserdata(L, 1);

	if (!m->closed)
	{
		mysql_close(m->mysql);
		m->closed = 1;
	}

	return 1;
}

static int mysql_escape(lua_State* L) {
	struct mysql_wrap *m = (struct mysql_wrap *)lua_touserdata(L, 1);
	char to[65535 * 2] = { 0 };

	std::size_t len;
	const char *s = lua_tolstring(L, -1, &len);

	std::size_t ret = mysql_real_escape_string(m->mysql, to, s, len);

	lua_pushlstring(L, to, ret);
	return 1;
}

static int mysql_new(lua_State *L) {
	MYSQL* mysql = mysql_init(NULL);

	if (mysql)
	{
		struct mysql_wrap * m = (struct mysql_wrap*)lua_newuserdata(L, sizeof(*m));
		m->mysql = mysql;
		m->closed = 1;

		if (luaL_newmetatable(L, "mysql_wrap")) {
			luaL_Reg l[] = {
				{ "connect", mysql_connect },
				{ "query", mysql_query },
				{ "close", mysql_close },
				{ "escape", mysql_escape },
				//{ "__gc", mysql_release },
				{ NULL, NULL },
			};
			luaL_newlib(L, l);
			lua_setfield(L, -2, "__index");
			lua_pushcfunction(L, mysql_release);
			lua_setfield(L, -2, "__gc");
		}
		lua_setmetatable(L, -2);

		//lua_pushlightuserdata(L, m);
		lua_pushnil(L);
		return 2;
	}
	else
	{
		lua_pushnil(L);
		lua_pushstring(L, "mysql_init failed");
		return 2;
	}
}

static int luaopen_mysql(lua_State *L)
{
	luaL_checkversion(L);

	luaL_Reg l[] = {
		{ "new", mysql_new },
		{ NULL, NULL },
	};

	luaL_newlibtable(L, l);

	lua_getfield(L, LUA_REGISTRYINDEX, "Context");

	Context *context = (Context*)lua_touserdata(L, -1);

	if (context == NULL)
	{
		return luaL_error(L, "please init CCActorContext...");
	}

	lua_getfield(L, LUA_REGISTRYINDEX, "SandBox");

	luaL_setfuncs(L, l, 2);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

#include "mysql.hpp"

using namespace tengine;

struct mysql
{
	MySql *imp;
	SandBox *self;
};

static int _mysql_query(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct mysql *my = (struct mysql*)lua_touserdata(L, 1);
	if (!my || !my->imp)
	{
		return luaL_error(L, "please new mysql first ...");
	}

	size_t len;

	const char * data = luaL_checklstring(L, 2, &len);

	luaL_checktype(L, 3, LUA_TFUNCTION);
	lua_pushvalue(L, 3);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);

	my->imp->query(data, len,
		[=](MySql *self, MYSQL *mysql, const char* err)
	{
		lua_State* L = my->self->state();

		lua_rawgeti(L, LUA_REGISTRYINDEX, callback);

		int type = lua_type(L, -1);

		if (err)
		{
			lua_pushstring(L, err);
			lua_pushnil(L);
		}
		else
		{
			lua_pushnil(L);

			int field_count = mysql_field_count(mysql);

			if (0 == field_count)
			{
				uint64_t affected_rows = mysql_affected_rows(mysql);

				uint64_t insert_id = mysql_insert_id(mysql);

				lua_newtable(L);
				lua_pushinteger(L, affected_rows);
				lua_setfield(L, -2, "affected_rows");

				lua_pushinteger(L, insert_id);
				lua_setfield(L, -2, "insert_id");

				lua_pushfstring(L, mysql_sqlstate(mysql));
				lua_setfield(L, -2, "server_status");

				lua_pushinteger(L, mysql_warning_count(mysql));
				lua_setfield(L, -2, "warning_count");
			}
			else
			{
				MYSQL_RES *result = mysql_store_result(mysql);

				if (result)
				{
					int num_fields = mysql_num_fields(result);

					MYSQL_FIELD ** fds = (MYSQL_FIELD **)ccmalloc(sizeof(MYSQL_FIELD *)* num_fields);
					MYSQL_FIELD * fd;
					for (int i = 0; fd = mysql_fetch_field(result); ++i)
					{
						fds[i] = fd;
					}

					lua_newtable(L);

					MYSQL_ROW row;
					int index = 0;
					while ((row = mysql_fetch_row(result)))
					{
						unsigned long *lengths;
						lengths = mysql_fetch_lengths(result);

						lua_newtable(L);
						for (int i = 0; i < num_fields; i++)
						{
							char* s = row[i];
							if (IS_NUM(fds[i]->type))
							{
								lua_pushnumber(L, atol(s));
							}
							else
							{
								lua_pushlstring(L, s, lengths[i]);
							}
							//lua_seti(L, -2, (i + 1));
							lua_setfield(L, -2, fds[i]->name);
						}
						index++;
						lua_seti(L, -2, index);
					}

					ccfree(fds);
					mysql_free_result(result);
				}
			}
		}

		my->self->call(2, true);

		luaL_unref(L, LUA_REGISTRYINDEX, callback);
	});

	return 0;
}

static int _mysql_release(lua_State *L)
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

static int mysql(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	const char *conf = luaL_checkstring(L, 1);

	MySql *mysql = new MySql(self);
	if (mysql == nullptr)
		return luaL_error(L, "create MySql failed !!!");

	if (mysql->start(conf) != 0)
		return luaL_error(L, "start MySql failed !!!");

	struct mysql *my = (struct mysql*)lua_newuserdata(L, sizeof(*my));
	my->imp = mysql;
	my->self = self;

	if (luaL_newmetatable(L, "mysql")) {
		luaL_Reg l[] = {
			{ "query", _mysql_query },
			{ "__gc", _mysql_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, _mysql_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &MySql::MYSQL_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &MySql::MYSQL_KEY);
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
