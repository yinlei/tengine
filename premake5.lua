-------------------------------------------------------------------------------
-- premake5.lua
-------------------------------------------------------------------------------

newoption {
	trigger = "lua",
	value = "LUA",
	description = "Choose lua version",
	allowed = {
		{"lua53", "LUA 5.3"},
		{"luajit", "LUA 5.1 LuaJit 2.0.4"}
	}
}

newoption {
    trigger = "malloc",
    value = "MALLOC",
    description = "choose a malloc",
    allowed = {
        {"malloc", "default malloc"},
        {"jemalloc", "jemalloc 3.6"},
        {"tcmalloc", "google tcmalloc"},
    }
}

print(_OPTIONS["malloc"])
if not _OPTIONS["lua"] then
   _OPTIONS["lua"] = "lua53"
end

if not _OPTIONS["malloc"] then
    _OPTIONS["malloc"] = "malloc"
end

workspace "ALL"
	configurations { "Debug32", "Release32", "Debug64", "Release64"}
	location "build"

	filter "configurations:*32"
		architecture "x86"
	filter{}

	filter "configurations:*64"
		architecture "x86_64"
	filter{}

	filter "configurations:Debug*"
		defines {
			"DEBUG",
			"LPEG_DEBUG"
		}
		flags { "Symbols" }
	filter{}

	filter "configurations:Release*"
     	defines { "NDEBUG" }
      	optimize "On"
    filter{}

if _OPTIONS["lua"] == "lua53" then
	project "lua"
		if os.is("windows") then
			kind "SharedLib"
			defines {
				"LUA_BUILD_AS_DLL",
			}
		else
			kind "StaticLib"
		end
		
		language "C"
		targetdir "./bin"
		includedirs {
			"./deps/lua/src",
		}
		files {
			"./deps/lua/src/*.c",
		}
		removefiles {
			"./deps/lua/src/luac.c",
			"./deps/lua/src/lua.c",
		}
end

project "hiredis"
	kind "StaticLib"
	language "C++"
	includedirs {
		"./deps/hiredis"
	}
	files {
		"./deps/hiredis/*.c",
		"./deps/hiredis/*.h"
	}
	removefiles {
		"./deps/hiredis/test.c",
	}

project "tengine"
	kind "ConsoleApp"
	language "C++"
	targetdir "./bin"
	defines {
		"ASIO_STANDALONE",
		"ASIO_HAS_STD_TYPE_TRAITS",
        "ASIO_HAS_STD_CHRONO",
	}

    if _OPTIONS["malloc"] == "jemalloc" then
        defines {
            "USE_JEMALLOC",
            "USE_STATIC",
        }
    end

	if os.is("windows") then
		defines{
			"_WIN32_WINNT=0x0501",
			"_CRT_SECURE_NO_WARNINGS"
		}
	end

	if os.get() == "linux" then
		buildoptions { "-std=c++11 -std=c++1y -fpermissive" }
	end

	includedirs {
		"./deps",
		"./deps/flatbuffers/include",
		"./deps/asio-1.11.0/",
	}

	if os.get() == "linux" then
		includedirs {"/usr/include/mysql/"}
	elseif os.get() == "windows" then
		includedirs {"./deps/mysql"}

        if _OPTIONS["malloc"] == "jemalloc" then
            includedirs {
                "./deps/jemalloc-win32/include/",
                "./deps/jemalloc-win32/include/msvc_compat/",
            }
        end
	end
	-- lua src
	files {
		"./src/*.cpp",
		"./src/*.hpp",
		"./src/*.h",
        "./src/*.c",
	}

	files {
		"./deps/lpeg-1.0.0/*c",
		"./deps/lua-snapshot/*.c",
		"./deps/lua-cmsgpack/*.c",
	}

	-- lua-cjson
	files {
		"./deps/lua-cjson/lua_cjson.h",
		"./deps/lua-cjson/lua_cjson.c",
		"./deps/lua-cjson/fpconv.c",
		"./deps/lua-cjson/strbuf.c",
	}

	removefiles {
		"./src/sandbox_*.cpp",
        "./src/system_info_*.cpp",
        "./src/process_info_*.cpp",
	}

    links {"hiredis"}

	if _OPTIONS["lua"] == "lua53" then
		includedirs {"./deps/lua/src"}
		links{"lua"}
	elseif _OPTIONS["lua"] == "luajit" then
		defines {"LUA_JIT"}

		files {"./deps/lua-compat-5.3/c-api/*.*"}
        files {"./deps/lua-compat-5.3/*.h"}
        files {"./deps/lua-compat-5.3/*.c"}

		includedirs {
			"./deps/LuaJIT-2.1.0-beta2/src",
			"./deps/lua-compat-5.3/"
		}
		libdirs {"./deps/LuaJIT-2.1.0-beta2/src"}
		links {"lua51"}
	end

	libdirs {"./deps/mysql"}

	if os.get() == "linux" then
		links {"./deps/mysql/mysqlclient_fix", "pthread", "stdc++fs", "dl"}
	elseif os.get() == "windows" then
		links {"libmySQL"}

        if _OPTIONS["malloc"] == "jemalloc" then
            libdirs {"./deps/jemalloc-win32/lib/vc2015/x86/"}
            links {"libjemalloc_x86_Debug"}
        end

    end

if _ACTION == "clean" then
	os.rmdir("build")
end
