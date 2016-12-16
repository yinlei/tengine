#include "sandbox.hpp"

#include "channel.hpp"

using namespace tengine;

struct channel 
{
	SandBox* self;
	Channel *imp;
	int on_connected_ref;
	int on_read_ref;
	int on_closed_ref;
};

static int channel_send(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct channel *c = (struct channel *)lua_touserdata(L, 1);
	if (!c)
	{
		return luaL_error(L, "please new channel first ...");
	}

	if (!c->imp)
		return luaL_error(L, "please new channel first ...");

	if (!c->imp->is_open())
		return luaL_error(L, "channel is not open");

	size_t len;

	const char * data = luaL_checklstring(L, 2, &len);

	c->imp->write(data, len);

	return 0;
}

static int channel_close(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct channel *c = (struct channel *)lua_touserdata(L, 1);
	if (!c)
	{
		return luaL_error(L, "please new channel first ...");
	}

	if (!c->imp)
		return luaL_error(L, "please new channel first ...");

	c->imp->close();

	lua_settop(L, 0);

	return 1;
}

static int channel_release(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct channel *c = (struct channel *)lua_touserdata(L, 1);
	if (c)
	{
		luaL_unref(L, LUA_REGISTRYINDEX, c->on_closed_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, c->on_connected_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, c->on_read_ref);
	}

	lua_rawgetp(L, LUA_REGISTRYINDEX, &Channel::CHANNEL_KEY);

	lua_pushlightuserdata(L, c->imp);
	lua_pushnil(L);
	lua_rawset(L, -3);

	//if (c && c->imp && c->imp->IsOpen())
	//	c->imp->Close();

	//delete c->imp;
	c->imp = NULL;
	c->self->channels.erase(c);

	lua_settop(L, 0);

	return 1;
}

static int channel_delete(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct channel *c = (struct channel *)lua_touserdata(L, 1);

	if (c)
	{
		lua_pushnil(L);
		lua_rawsetp(L, LUA_REGISTRYINDEX, c->imp);

		luaL_unref(L, LUA_REGISTRYINDEX, c->on_closed_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, c->on_connected_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, c->on_read_ref);

		//c->imp->Close();
	}

	return 1;
}

static int channel(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	const char *address = luaL_checkstring(L, 1);
	if (address == NULL)
		return luaL_error(L, "channel need address");

	const char* port = luaL_checkstring(L, 2);

	// callback
	luaL_checktype(L, 3, LUA_TTABLE);
	//lua_settop(L, 3);

	lua_getfield(L, 3, "on_connected");
	int connected_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	lua_getfield(L, 3, "on_read");
	int read_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	lua_getfield(L, 3, "on_closed");
	int closed_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	ChannelPtr channel(new Channel(self));
	if (channel == NULL)
		return luaL_error(L, "create channel failed");

	channel->connect(address, port);

	struct channel *c = (struct channel*)lua_newuserdata(L, sizeof(*c));
	c->self = self;
	c->imp = channel.get();
	c->on_connected_ref = connected_handler;
	c->on_read_ref = read_handler;
	c->on_closed_ref = closed_handler;
	//lua_rawsetp(L, LUA_REGISTRYINDEX, channel);
	//lua_rawgetp(L, LUA_REGISTRYINDEX, channel);
	if (luaL_newmetatable(L, "channel")) {
		luaL_Reg l[] = {
			{ "send", channel_send },
			{ "close", channel_close },
			{ "delete", channel_delete },
			{ "__gc", channel_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, channel_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	//luaL_getsubtable(L, LUA_REGISTRYINDEX, "channel");
	//lua_pushstring(L, "kv");
	//lua_setfield(L, -2, "__mode");  /** hooktable.__mode = "k" */
	//lua_pushvalue(L, -2);
	//lua_rawsetp(L, -1, channel);
	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &Channel::CHANNEL_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &Channel::CHANNEL_KEY);
		lua_pushstring(L, "kv");
		lua_setfield(L, -2, "__mode");
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
	}

	lua_pushlightuserdata(L, c->imp);
	lua_pushvalue(L, -3);
	//lua_rawset(L, -3);
	lua_settable(L, -3);

	lua_pop(L, 1);
	//lua_settop(L, 0);
	//lua_rawsetp(L, LUA_REGISTRYINDEX, channel);
	//lua_rawgetp(L, LUA_REGISTRYINDEX, channel);

	self->channels[c] = channel;
	return 1;
}

void SandBox::channel_connected(void* sender)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);

	lua_rawgetp(L, LUA_REGISTRYINDEX, &Channel::CHANNEL_KEY);

	lua_rawgetp(L, -1, sender);

	struct channel* c = (struct channel*)lua_touserdata(L, -1);
	if (c != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, c->on_connected_ref);

		call(0, true);
	}
}

void SandBox::channel_read(void* sender, const char* data, std::size_t size)
{
	lua_State *L = l_;
	//lua_rawgetp(L, LUA_REGISTRYINDEX, message->sender);

	lua_rawgetp(L, LUA_REGISTRYINDEX, &Channel::CHANNEL_KEY);

	lua_rawgetp(L, -1, sender);

	struct channel* c = (struct channel*)lua_touserdata(L, -1);
	if (c != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, c->on_read_ref);
		lua_pushlstring(L, data, size);
		lua_pushinteger(L, size);
		call(2, true);
	}

	ccfree((void*)data);
}

void SandBox::channel_closed(void* sender, const char* error)
{
	lua_State *L = l_;

	lua_rawgetp(L, LUA_REGISTRYINDEX, &Channel::CHANNEL_KEY);

	lua_rawgetp(L, -1, sender);

	struct channel* c = (struct channel*)lua_touserdata(L, -1);
	if (c != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, c->on_closed_ref);
		lua_pushstring(L, error);
		call(1, true);
	}

	ccfree((void*)error);
}

///////////////////////////////////////////////////////////////////////////////

struct udp_channel
{
	SandBox* self;
	UdpChannel *imp;
	int on_read_ref;
};

static int udp_channel_send_to(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_channel *c = (struct udp_channel *)lua_touserdata(L, 1);
	if (!c)
	{
		return luaL_error(L, "please new channel first ...");
	}

	if (!c->imp)
		return luaL_error(L, "please new channel first ...");

	size_t len;

	const char * data = luaL_checklstring(L, 2, &len);

	len = c->imp->send_to(data, len);

	lua_pushinteger(L, len);

	return 1;
}

static int udp_channel_async_send_to(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_channel *c = (struct udp_channel *)lua_touserdata(L, 1);
	if (!c)
	{
		return luaL_error(L, "please new channel first ...");
	}

	if (!c->imp)
		return luaL_error(L, "please new channel first ...");

	size_t len;

	const char * data = luaL_checklstring(L, 2, &len);

	c->imp->async_send_to(data, len);

	return 0;
}

static int udp_channel_close(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_channel *c = (struct udp_channel *)lua_touserdata(L, 1);
	if (!c)
	{
		return luaL_error(L, "please new udp_channel first ...");
	}

	if (!c->imp)
		return luaL_error(L, "please new udp_channel first ...");

	c->imp->close();

	lua_settop(L, 0);

	return 1;
}

static int udp_channel_release(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_channel *c = (struct udp_channel *)lua_touserdata(L, 1);
	if (c)
	{
		luaL_unref(L, LUA_REGISTRYINDEX, c->on_read_ref);
	}

	lua_rawgetp(L, LUA_REGISTRYINDEX, &UdpChannel::UDPCHANNEL_KEY);

	lua_pushlightuserdata(L, c->imp);
	lua_pushnil(L);
	lua_rawset(L, -3);

	c->imp = NULL;
	c->self->udp_channels.erase(c);

	lua_settop(L, 0);

	return 1;
}

static int udp_channel(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	const char *address = luaL_checkstring(L, 1);
	if (address == NULL)
		return luaL_error(L, "channel need address");

	const char* port = luaL_checkstring(L, 2);

	// callback
	luaL_checktype(L, 3, LUA_TTABLE);
	//lua_settop(L, 3);

	lua_getfield(L, 1, "on_read");
	int read_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	UdpChannelPtr channel(new UdpChannel(self, address, port));
	if (!channel)
		return luaL_error(L, "create udp_channel failed");

	struct udp_channel *c = (struct udp_channel*)lua_newuserdata(L, sizeof(*c));
	c->self = self;
	c->imp = channel.get();
	c->on_read_ref = read_handler;

	if (luaL_newmetatable(L, "udp_channel")) {
		luaL_Reg l[] = {
			{ "asendto", udp_channel_async_send_to },
			{ "sendto", udp_channel_send_to },
			{ "close", udp_channel_close },
			{ "__gc", udp_channel_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, udp_channel_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &UdpChannel::UDPCHANNEL_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &UdpChannel::UDPCHANNEL_KEY);
		lua_pushstring(L, "kv");
		lua_setfield(L, -2, "__mode");
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
	}

	lua_pushlightuserdata(L, c->imp);
	lua_pushvalue(L, -3);
	//lua_rawset(L, -3);
	lua_settable(L, -3);

	lua_pop(L, 1);

	self->udp_channels[c] = channel;
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
struct udp_sender
{
	SandBox* self;
	UdpSender *imp;
	int on_read_ref;
};

static int udp_sender_send_to(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_sender *c = (struct udp_sender *)lua_touserdata(L, 1);
	if (!c)
		return luaL_error(L, "please new channel first ...");

	if (!c->imp)
		return luaL_error(L, "please new channel first ...");

	size_t len;

	const char * data = luaL_checklstring(L, 2, &len);

	const char * address = luaL_checkstring(L, 3);

	int port = (int)luaL_checkinteger(L, 4);

	len = c->imp->send_to(data, len, address, port);

	lua_pushinteger(L, len);

	return 1;
}

static int udp_sender_async_send_to(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_sender *c = (struct udp_sender *)lua_touserdata(L, 1);
	if (!c)
		return luaL_error(L, "please new channel first ...");

	if (!c->imp)
		return luaL_error(L, "please new channel first ...");

	size_t len;

	const char * data = luaL_checklstring(L, 2, &len);

	const char * address = luaL_checkstring(L, 3);

	int port = (int)luaL_checkinteger(L, 4);

	c->imp->async_send_to(data, len, address, port);

	return 0;
}

static int udp_sender_close(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_sender *c = (struct udp_sender *)lua_touserdata(L, 1);
	if (!c)
	{
		return luaL_error(L, "please new udp_channel first ...");
	}

	if (!c->imp)
		return luaL_error(L, "please new udp_channel first ...");

	c->imp->close();

	lua_settop(L, 0);

	return 1;
}

static int udp_sender_release(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TUSERDATA);

	struct udp_sender *c = (struct udp_sender *)lua_touserdata(L, 1);
	if (c)
	{
		luaL_unref(L, LUA_REGISTRYINDEX, c->on_read_ref);
	}

	lua_rawgetp(L, LUA_REGISTRYINDEX, &UdpSender::UDPSENDER_KEY);

	lua_pushlightuserdata(L, c->imp);
	lua_pushnil(L);
	lua_rawset(L, -3);

	c->imp = NULL;
	c->self->udp_senders.erase(c);

	lua_settop(L, 0);

	return 1;
}

static int udp_sender(lua_State *L)
{
	Context *context = (Context*)lua_touserdata(L, lua_upvalueindex(1));

	SandBox *self = (SandBox*)lua_touserdata(L, lua_upvalueindex(2));

	// callback
	luaL_checktype(L, 1, LUA_TTABLE);
	//lua_settop(L, 3);

	lua_getfield(L, 1, "on_read");
	int read_handler = luaL_ref(L, LUA_REGISTRYINDEX);
	//lua_pop(L, 1);

	UdpSenderPtr sender(new UdpSender(self));
	if (!sender)
		return luaL_error(L, "create udp_channel failed");

	struct udp_sender *c = (struct udp_sender*)lua_newuserdata(L, sizeof(*c));
	c->self = self;
	c->imp = sender.get();
	c->on_read_ref = read_handler;

	if (luaL_newmetatable(L, "udp_sender")) {
		luaL_Reg l[] = {
			{ "asendto", udp_sender_async_send_to },
			{ "sendto", udp_sender_send_to },
			{ "close", udp_sender_close },
			{ "__gc", udp_sender_release },
			{ NULL, NULL },
		};
		luaL_newlib(L, l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, udp_sender_release);
		lua_setfield(L, -2, "__gc");
	}

	lua_setmetatable(L, -2);

	if (lua_rawgetp(L, LUA_REGISTRYINDEX, &UdpSender::UDPSENDER_KEY) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_createtable(L, 0, 2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &UdpSender::UDPSENDER_KEY);
		lua_pushstring(L, "kv");
		lua_setfield(L, -2, "__mode");
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
	}

	lua_pushlightuserdata(L, c->imp);
	lua_pushvalue(L, -3);
	//lua_rawset(L, -3);
	lua_settable(L, -3);

	lua_pop(L, 1);

	self->udp_senders[c] = sender;
	return 1;
}


void SandBox::udp_channel_read(void* sender, const std::string& address, uint16_t port, const char* data, std::size_t size)
{
	lua_State *L = l_;

	lua_rawgetp(L, LUA_REGISTRYINDEX, &UdpChannel::UDPCHANNEL_KEY);

	lua_rawgetp(L, -1, sender);

	struct udp_channel* c = (struct udp_channel*)lua_touserdata(L, -1);
	if (c != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, c->on_read_ref);
		lua_pushlstring(L, data, size);
		//lua_pushlstring(L, read_message->address()->c_str(), read_message->address()->size());
		//lua_pushinteger(L, read_message->port());
		call(3, true);
	}
}

void SandBox::udp_sender_read(void* sender, const std::string& address, uint16_t port, const char* data, std::size_t size)
{
	lua_State *L = l_;

	lua_rawgetp(L, LUA_REGISTRYINDEX, &UdpSender::UDPSENDER_KEY);

	lua_rawgetp(L, -1, sender);

	struct udp_sender* c = (struct udp_sender*)lua_touserdata(L, -1);
	if (c != NULL)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, c->on_read_ref);
		lua_pushlstring(L, data, size);
		lua_pushlstring(L, address.c_str(), address.size());
		lua_pushinteger(L, port);
		call(3, true);
	}
}