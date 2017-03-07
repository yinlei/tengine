-------------------------------------------------------------------------------
-- wrap for c.web
-------------------------------------------------------------------------------
local _PACKAGE = (...):match("^(.+)[%./][^%./]+") or ""

local string, setmetatable = string, setmetatable

local string_format = string.format
local string_len = string.len

local coroutine_running = coroutine.running
local coroutine_resume = coroutine.resume
local coroutine_yield = coroutine.yield

local c = tengine.c

local co_pool = require(_PACKAGE .. "/pool")

local HEADER = [[
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Content-Length: " %d "\r\n\r\n"
]]

local get = function(self, path, handler)
    local _methods = self.router["GET"] or {}
    
    _methods[path] = handler

    self.router["GET"] = _methods
end

local post = function(self, path, handler)
    local _methods = self.router["POST"] or {}
    
    _methods[path] = handler

    self.router["POST"] = _methods
end

local handle = function(self, res, type, path, content)
    local handler = self.router[type][path]
    if handler then
        local ok, ret = pcall(handler, content)
        if not ok  then
            self.web:response(res, '', 1)
            error(ret)
        else
            --self.web:response(res, string_format(HEADER, string_len(ret)))
            self.web:response(res, "HTTP/1.1 200 OK\r\n")
            self.web:response(res, "Content-Type: application/json\r\n")
            self.web:response(res, "Access-Control-Allow-Origin: *\r\n")
            self.web:response(res, "Content-Length: " .. string_len(ret) .. "\r\n\r\n")
            self.web:response(res, ret, 1)
        end
    end
end

local methods = {
    get = get,
    post = post,
}

local new = function(port)
    local self = setmetatable({}, {__index = methods})

    self.port = port

    self.router = {}

    self.web = c.web()

    if self.web then
        self.web:start(port, function(res, type, path, content)
            local co = co_pool.new(handle)
            local succ, ret = coroutine_resume(co, self, res, type, path, content)
            if not succ then
                error(err)
            end
        end)
    else
        error("create web failed !!!")
    end

    return self
end

return {
    new = new
}
