newoption({
	trigger = "graphics",
	value = "OPENGL_VERSION",
	description = "version of OpenGL to build raylib against",
	allowed = {
		{ "opengl11", "OpenGL 1.1" },
		{ "opengl21", "OpenGL 2.1" },
		{ "opengl33", "OpenGL 3.3" },
		{ "opengl43", "OpenGL 4.3" },
		{ "openges2", "OpenGL ES2" },
		{ "openges3", "OpenGL ES3" },
		{ "software", "OpenGL 1.1 Software Render" },
	},
	default = "opengl33",
})

newoption({
	trigger = "backend",
	value = "BACKEND_TYPE",
	description = "Backend Platform to use",
	allowed = {
		{ "glfw", "GLFW" },
		{ "rgfw", "RGFW" },
		{ "win32", "WIN32" },
	},
	default = "glfw",
})

newoption({
	trigger = "wayland",
	value = "WAYLAND",
	description = "build for wayland",
	allowed = {
		{ "off", "Off" },
		{ "on", "On" },
	},
	default = "off",
})

newoption({
	trigger = "with-emscripten",
	description = "Build for web using Emscripten",
})

newoption({
	trigger = "itchio",
	description = "Use the itch.io web shell (src/app/itchio.html) instead of index.html",
})

newoption({
	trigger = "perf",
	value = "MODE",
	description = "extra optimization flags for Release gmake/GCC builds",
	allowed = {
		{ "none", "stock -O2 (default)" },
		{ "fast", "-O3 -flto" },
		{ "avx2", "-O3 -flto -mavx2 -mfma -mbmi2" },
		{ "native", "-O3 -flto -march=native -mtune=native" },
	},
	default = "none",
})

local perf_table = {
	none = {},
	fast = { "-O3", "-flto" },
	avx2 = { "-O3", "-flto", "-mavx2", "-mfma", "-mbmi2" },
	native = { "-O3", "-flto", "-march=native", "-mtune=native" },
}
local perf_flags = perf_table[_OPTIONS["perf"] or "none"]

local function apply_perf_flags()
	if not perf_flags or #perf_flags == 0 then
		return
	end
	filter({})
	filter({ "action:gmake*", "toolset:gcc or clang", "configurations:Release" })
	buildoptions(perf_flags)
	if perf_flags[2] == "-flto" then
		linkoptions({ "-flto" })
	end
	filter({})
end

function download_progress(total, current)
	local ratio = current / total
	ratio = math.min(math.max(ratio, 0), 1)
	local percent = math.floor(ratio * 100)
	print("Download progress (" .. percent .. "%/100%)")
end

function check_raylib()
	os.chdir("external")
	if os.isdir("raylib-master") == false then
		if not os.isfile("raylib-master.zip") then
			print("Raylib not found, downloading from github")
			local result_str, response_code =
				http.download("https://github.com/raysan5/raylib/archive/refs/heads/master.zip", "raylib-master.zip", {
					progress = download_progress,
					headers = { "From: Premake", "Referer: Premake" },
				})
		end
		print("Unzipping to " .. os.getcwd())
		zip.extract("raylib-master.zip", os.getcwd())
		os.remove("raylib-master.zip")
	end
	os.chdir("../")
end

function build_externals()
	print("calling externals")
	check_raylib()
end

function platform_defines()
	filter({ "options:with-emscripten" })
	defines({ "PLATFORM_WEB" })

	filter({ "options:backend=glfw", "options:not with-emscripten" })
	defines({ "PLATFORM_DESKTOP" })

	filter({ "options:backend=rgfw" })
	defines({ "PLATFORM_DESKTOP_RGFW" })

	filter({ "options:backend=win32" })
	defines({ "PLATFORM_DESKTOP_WIN32" })

	filter({ "options:graphics=opengl43", "options:not with-emscripten" })
	defines({ "GRAPHICS_API_OPENGL_43" })

	filter({ "options:graphics=opengl33", "options:not with-emscripten" })
	defines({ "GRAPHICS_API_OPENGL_33" })

	filter({ "options:graphics=opengl21", "options:not with-emscripten" })
	defines({ "GRAPHICS_API_OPENGL_21" })

	filter({ "options:graphics=opengl11", "options:not with-emscripten" })
	defines({ "GRAPHICS_API_OPENGL_11" })

	filter({ "options:graphics=openges3", "options:not with-emscripten" })
	defines({ "GRAPHICS_API_OPENGL_ES3" })

	filter({ "options:graphics=openges2", "options:not with-emscripten" })
	defines({ "GRAPHICS_API_OPENGL_ES2" })

	filter({ "options:graphics=software", "options:not with-emscripten" })
	defines({ "GRAPHICS_API_OPENGL_11_SOFTWARE" })

	-- Emscripten only supports OpenGL ES (WebGL); default to ES2 even if --graphics unset
	filter({ "options:with-emscripten", "options:graphics=openges3" })
	defines({ "GRAPHICS_API_OPENGL_ES3" })

	filter({ "options:with-emscripten" })
	defines({ "GRAPHICS_API_OPENGL_ES2" })

	filter({ "system:macosx" })
	disablewarnings({ "deprecated-declarations" })

	filter({ "system:linux", "options:wayland=off" })
	defines({ "_GLFW_X11" })

	filter({ "system:linux", "options:wayland=on" })
	defines({ "_GLFW_WAYLAND" })

	filter({})
end

-- if you don't want to download raylib, then set this to false, and set the raylib dir to where you want raylib to be pulled from, must be full sources.
downloadRaylib = true

-- Resolve the repository root from this script's own location, not from the
-- working directory. path.getabsolute("..") is relative to wherever premake
-- happened to be invoked, which silently baked the original author's home
-- directory into the generated makefiles and broke every copy, icon and font
-- path for anyone else.
--
-- _SCRIPT_DIR is already the directory holding this script (build/), so the
-- repository root is its parent.
local ROOT = path.getabsolute(path.join(path.getabsolute(_SCRIPT_DIR), ".."))
raylib_dir = path.join(ROOT, "build/external/raylib-master")

-- The workspace is always named after the repository directory, so a clone
-- called `Referentia` produces `Referentia.sln` and `referentia` targets
-- without any further configuration. Derived from ROOT for the same reason.
workspaceName = path.getbasename(ROOT)

if os.isdir("build_files") == false then
	os.mkdir("build_files")
end

if os.isdir("external") == false then
	os.mkdir("external")
end

workspace(workspaceName)
location("../")
configurations({ "Debug", "Release" })

filter({ "options:not with-emscripten" })
platforms({ "x64", "x86", "ARM64" })
defaultplatform("x64")

filter({ "options:with-emscripten" })
platforms({ "Web" })
defaultplatform("Web")

filter("configurations:Debug")
defines({ "DEBUG", "DEBUG_MODE", "LOG_LEVEL_DEBUG" })
symbols("On")

filter("configurations:Release")
defines({ "NDEBUG" })
optimize("On")

filter({ "configurations:Release", "action:vs*" })
linktimeoptimization("On")

filter({ "platforms:x64" })
architecture("x86_64")

filter({ "platforms:ARM64" })
architecture("ARM64")

filter({})

targetdir("bin/%{cfg.buildcfg}/")

if downloadRaylib then
	build_externals()
end

startproject(workspaceName)

project(workspaceName)
kind("ConsoleApp")
location("build_files/")
targetdir("../bin/%{cfg.buildcfg}")
targetname("referentia")

filter({ "system:windows", "action:gmake*", "options:not with-emscripten" })
files({ "../src/app/*.rc", "../src/app/*.ico" })
prebuildcommands({
	'windres "' .. ROOT .. '/src/app/application.rc" -O coff -o "%{cfg.buildtarget.directory}/application_res.o"',
})
-- Copy the bundled fonts next to the binary so the app finds them at
-- "fonts/WorkSans-*.ttf" from its working directory.
postbuildcommands({
	'mkdir -p "%{cfg.targetdir}/fonts"',
	'cp "' .. ROOT .. '/resources/fonts/"*.ttf "%{cfg.targetdir}/fonts/"',
	'cp "' .. ROOT .. '/resources/fonts/OFL.txt" "%{cfg.targetdir}/fonts/"',
})
linkoptions({
	"%{cfg.buildtarget.directory}/application_res.o",
})

filter({ "system:windows", "action:vs*", "options:not with-emscripten" })
files({ "../src/app/*.rc", "../src/app/*.ico" })

filter({ "system:linux", "options:not with-emscripten" })
local binDir = path.getabsolute("../bin/%{cfg.buildcfg}")
postbuildcommands({
	-- Copy the icon
	'cp "'
		.. ROOT
		.. '/resources/icon.png" "'
		.. binDir
		.. '/icon.png"',

	-- Copy the bundled fonts (and the licence that must ship with them) so the
	-- app finds them at "fonts/WorkSans-*.ttf" next to the binary. Without
	-- this every string falls back to raylib's default font.
	'mkdir -p "' .. binDir .. '/fonts"',
	'cp "' .. ROOT .. '/resources/fonts/"*.ttf "' .. binDir .. '/fonts/"',
	'cp "' .. ROOT .. '/resources/fonts/OFL.txt" "' .. binDir .. '/fonts/"',

	-- Generate desktop file line by line
	'echo "[Desktop Entry]" > "'
		.. binDir
		.. '/referentia.desktop"',
	'echo "Name=Referentia" >> "'
		.. binDir
		.. '/referentia.desktop"',
	'echo "Exec='
		.. binDir
		.. '/referentia" >> "'
		.. binDir
		.. '/referentia.desktop"',
	'echo "Icon='
		.. binDir
		.. '/icon.png" >> "'
		.. binDir
		.. '/referentia.desktop"',
	'echo "Type=Application" >> "' .. binDir .. '/referentia.desktop"',
	'echo "Categories=Graphics;Utility;" >> "' .. binDir .. '/referentia.desktop"',
	'echo "MimeType=image/png;image/jpeg;image/webp;" >> "' .. binDir .. '/referentia.desktop"',
	-- A .desktop entry otherwise launches with $HOME as the working
	-- directory, which would leave the font lookup above unresolved.
	'echo "Path=' .. binDir .. '" >> "' .. binDir .. '/referentia.desktop"',
	'chmod +x "' .. binDir .. '/referentia.desktop"',
})

filter({ "system:macosx" })
postbuildcommands({
	'cp "' .. ROOT .. '/resources/icon.png" "%{cfg.targetdir}/icon.png"',
	'mkdir -p "%{cfg.targetdir}/fonts"',
	'cp "' .. ROOT .. '/resources/fonts/"*.ttf "%{cfg.targetdir}/fonts/"',
	'cp "' .. ROOT .. '/resources/fonts/OFL.txt" "%{cfg.targetdir}/fonts/"',
})


filter({ "system:macosx" })
postbuildcommands({
	'cp "' .. ROOT .. '/resources/icon.png" "%{cfg.targetdir}/icon.png"',
})

filter({})

-- Emscripten Web Build Configuration
local web_shell_file = "index.html"
local web_target_name = "referentia"
local web_standalone = false
if _OPTIONS["itchio"] then
	-- itch.io serves the html file directly; it expects index.html
	web_shell_file = "itchio.html"
	web_target_name = "index"
	-- Embed everything (wasm + assets) into the html so it is a single
	-- self-contained file: double-clicking index.html plays the sim with
	-- no web server (and itch.io accepts a single html upload).
	web_standalone = true
end

filter({ "options:with-emscripten" })
kind("ConsoleApp")
targetextension(".html")
targetname(web_target_name)
buildoptions({
	"-sGL_ENABLE_GET_PROC_ADDRESS",
})
if not web_standalone then
	linkoptions({
		"-s USE_GLFW=3",
		"-s ASYNCIFY",
		"-s TOTAL_MEMORY=67108864",
		"-s FORCE_FILESYSTEM=1",
		"-s ALLOW_MEMORY_GROWTH=1",
		"-s EXPORTED_FUNCTIONS=['_main']",
		"-s EXPORTED_RUNTIME_METHODS=ccall",
		"-s OFFSCREENCANVAS_SUPPORT=1 ",
		"-s GL_EMULATE_GLES_VERSION_STRING_FORMAT=1 ",
		"--no-heap-copy",
		"--preload-file ../../resources@/resources",
		"--shell-file ../../src/app/" .. web_shell_file,
		"-s FULL_ES2=1",
		"-sGL_ENABLE_GET_PROC_ADDRESS",
	})
else
	linkoptions({
		"-s USE_GLFW=3",
		"-s ASYNCIFY",
		"-s TOTAL_MEMORY=67108864",
		"-s FORCE_FILESYSTEM=1",
		"-s ALLOW_MEMORY_GROWTH=1",
		"-s EXPORTED_FUNCTIONS=['_main']",
		"-s EXPORTED_RUNTIME_METHODS=ccall",
		"-s OFFSCREENCANVAS_SUPPORT=1 ",
		"-s GL_EMULATE_GLES_VERSION_STRING_FORMAT=1 ",
		"-s SINGLE_FILE=1",
		"--no-heap-copy",
		-- Embed the assets the app loads at runtime. All nine Work Sans
		-- statics (Thin..Black, upright and italic) have to be embedded
		-- even though only three are rasterised at startup: app::GetFont
		-- loads a face the first time something draws with that weight, so
		-- a face that is not embedded is not merely unused, it is a face
		-- that degrades to raylib's fallback font the moment it is reached.
		-- Mounted under /fonts (the path app::ResolveFontPath probes
		-- first) and /resources/fonts as a fallback for the preload layout.
		"--embed-file ../../resources/fonts/WorkSans-Thin.ttf@/fonts/WorkSans-Thin.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ThinItalic.ttf@/fonts/WorkSans-ThinItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ExtraLight.ttf@/fonts/WorkSans-ExtraLight.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ExtraLightItalic.ttf@/fonts/WorkSans-ExtraLightItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Light.ttf@/fonts/WorkSans-Light.ttf",
		"--embed-file ../../resources/fonts/WorkSans-LightItalic.ttf@/fonts/WorkSans-LightItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Regular.ttf@/fonts/WorkSans-Regular.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Italic.ttf@/fonts/WorkSans-Italic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Medium.ttf@/fonts/WorkSans-Medium.ttf",
		"--embed-file ../../resources/fonts/WorkSans-MediumItalic.ttf@/fonts/WorkSans-MediumItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-SemiBold.ttf@/fonts/WorkSans-SemiBold.ttf",
		"--embed-file ../../resources/fonts/WorkSans-SemiBoldItalic.ttf@/fonts/WorkSans-SemiBoldItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Bold.ttf@/fonts/WorkSans-Bold.ttf",
		"--embed-file ../../resources/fonts/WorkSans-BoldItalic.ttf@/fonts/WorkSans-BoldItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ExtraBold.ttf@/fonts/WorkSans-ExtraBold.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ExtraBoldItalic.ttf@/fonts/WorkSans-ExtraBoldItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Black.ttf@/fonts/WorkSans-Black.ttf",
		"--embed-file ../../resources/fonts/WorkSans-BlackItalic.ttf@/fonts/WorkSans-BlackItalic.ttf",
		"--embed-file ../../resources/fonts/OFL.txt@/fonts/OFL.txt",
		"--embed-file ../../resources/fonts/WorkSans-Thin.ttf@/resources/fonts/WorkSans-Thin.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ThinItalic.ttf@/resources/fonts/WorkSans-ThinItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ExtraLight.ttf@/resources/fonts/WorkSans-ExtraLight.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ExtraLightItalic.ttf@/resources/fonts/WorkSans-ExtraLightItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Light.ttf@/resources/fonts/WorkSans-Light.ttf",
		"--embed-file ../../resources/fonts/WorkSans-LightItalic.ttf@/resources/fonts/WorkSans-LightItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Regular.ttf@/resources/fonts/WorkSans-Regular.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Italic.ttf@/resources/fonts/WorkSans-Italic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Medium.ttf@/resources/fonts/WorkSans-Medium.ttf",
		"--embed-file ../../resources/fonts/WorkSans-MediumItalic.ttf@/resources/fonts/WorkSans-MediumItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-SemiBold.ttf@/resources/fonts/WorkSans-SemiBold.ttf",
		"--embed-file ../../resources/fonts/WorkSans-SemiBoldItalic.ttf@/resources/fonts/WorkSans-SemiBoldItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Bold.ttf@/resources/fonts/WorkSans-Bold.ttf",
		"--embed-file ../../resources/fonts/WorkSans-BoldItalic.ttf@/resources/fonts/WorkSans-BoldItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ExtraBold.ttf@/resources/fonts/WorkSans-ExtraBold.ttf",
		"--embed-file ../../resources/fonts/WorkSans-ExtraBoldItalic.ttf@/resources/fonts/WorkSans-ExtraBoldItalic.ttf",
		"--embed-file ../../resources/fonts/WorkSans-Black.ttf@/resources/fonts/WorkSans-Black.ttf",
		"--embed-file ../../resources/fonts/WorkSans-BlackItalic.ttf@/resources/fonts/WorkSans-BlackItalic.ttf",
		"--embed-file ../../resources/fonts/OFL.txt@/resources/fonts/OFL.txt",
		"--shell-file ../../src/app/" .. web_shell_file,
		"-s FULL_ES2=1",
		"-sGL_ENABLE_GET_PROC_ADDRESS",
	})
end

	-- Zip the web outputs for release/distribution. Named referentia-itchio.zip
	-- when --itchio is used (ready for the itch.io upload form, containing
	-- index.html), otherwise referentia-web.zip. The favicon (icon.png) is copied
	-- to the output folder and bundled in the zip too (the shell links it).
	local web_zip_name = "referentia-web.zip"
	if _OPTIONS["itchio"] then
		web_zip_name = "referentia-itchio.zip"
	end
	local web_zip_entries
	if web_standalone then
		web_zip_entries = "index.html icon.png"
	else
		web_zip_entries = web_target_name .. ".html " .. web_target_name .. ".js " .. web_target_name .. ".wasm " .. web_target_name .. ".data icon.png"
	end
	local scripts_dir = path.getabsolute("../scripts")
postbuildcommands({
	'cp "' .. ROOT .. '/resources/icon.png" "%{cfg.targetdir}/icon.png"',
	'python3 "' .. scripts_dir .. '/package_release.py" "%{cfg.targetdir}" --out "%{cfg.targetdir}/' .. web_zip_name .. '" ' .. web_zip_entries,
})

filter({ "options:with-emscripten", "configurations:Release" })
buildoptions({ "-Os" })
linkoptions({ "-Os" })

filter({ "options:with-emscripten", "configurations:Debug" })
buildoptions({ "-g" })
linkoptions({ "-g", "-s ASSERTIONS=1" })

filter({ "system:windows", "configurations:Release", "action:gmake*", "options:not with-emscripten" })
kind("WindowedApp")
buildoptions({ "-Wl,--subsystem,windows" })

filter({ "system:windows", "configurations:Release", "action:vs*" })
kind("WindowedApp")
entrypoint("mainCRTStartup")

filter("action:vs*")
debugdir("$(SolutionDir)")

filter({ "action:gmake*" }) -- Uncoment if you need to force StaticLib
--          buildoptions { "-static" }
filter({})

vpaths({
	["Header Files/*"] = { "../include/**.h", "../include/**.hpp", "../src/**.h", "../src/**.hpp" },
	["Source Files/*"] = { "../src/**.c", "src/**.cpp" },
	["Windows Resource Files/*"] = { "../src/app/**.rc", "src/app/**.ico" },
})

files({ "../src/**.c", "../src/**.cpp", "../src/**.h", "../src/**.hpp", "../include/**.h", "../include/**.hpp" })
removefiles({ "../src/tests/**.cpp", "../src/tests/**.h" })

-- For web builds, include raylib source files directly
filter({ "options:with-emscripten" })
files({
	raylib_dir .. "/src/rcore.c",
	raylib_dir .. "/src/rshapes.c",
	raylib_dir .. "/src/rtextures.c",
	raylib_dir .. "/src/rtext.c",
	raylib_dir .. "/src/rmodels.c",
	raylib_dir .. "/src/raudio.c",
})
defines({ "PLATFORM_WEB" })

filter({ "system:windows", "action:vs*", "options:not with-emscripten" })
files({ "../src/app/*.rc", "../src/app/*.ico" })

filter({})

includedirs({ "../src" })
includedirs({ "../include" })

filter({ "options:not with-emscripten" })
links({ "raylib" })

filter({})

filter({ "options:with-emscripten" })
cdialect("gnu17")
cppdialect("gnu++17")

filter({ "options:not with-emscripten" })
cdialect("C17")
cppdialect("C++17")

filter({})

includedirs({ raylib_dir .. "/src" })

filter({ "toolset:gcc or clang" })
buildoptions({ "-Wshadow" })

apply_perf_flags()

filter("action:vs*")
buildoptions({ "/w34456" })

filter({})
platform_defines()

filter("action:vs*")
defines({ "_WINSOCK_DEPRECATED_NO_WARNINGS", "_CRT_SECURE_NO_WARNINGS" })
dependson({ "raylib" })
links({ "raylib.lib" })
characterset("Unicode")
buildoptions({ "/Zc:__cplusplus" })

filter({ "system:windows", "options:not with-emscripten" })
	defines({ "_WIN32", "NOMINMAX" })
links({ "winmm", "gdi32", "opengl32" })
libdirs({ "../bin/%{cfg.buildcfg}" })

filter({ "system:linux", "options:not with-emscripten" })
links({ "pthread", "m", "dl", "rt" })

filter({ "system:linux", "options:wayland=off", "options:not with-emscripten" })
links({ "X11" })

filter({ "system:linux", "options:wayland=on", "options:not with-emscripten" })
links({ "wayland-client", "wayland-cursor", "wayland-egl", "xkbcommon" })

filter({ "system:macosx", "options:not with-emscripten" })
links({
	"OpenGL.framework",
	"Cocoa.framework",
	"IOKit.framework",
	"CoreFoundation.framework",
	"CoreAudio.framework",
	"CoreVideo.framework",
	"AudioToolbox.framework",
	"QuartzCore.framework",
})

filter({})

-- Only build raylib library for non-web builds
if not _OPTIONS["with-emscripten"] then
	group("Dependencies")

	project("raylib")
	kind("StaticLib")

	platform_defines()

	location("build_files/")

	language("C")
	targetdir("../bin/%{cfg.buildcfg}")

	filter({ "options:wayland=on" })
	defines({ "GLFW_LINUX_ENABLE_WAYLAND=TRUE" })

	filter({ "options:wayland=on", "system:linux" })
	prebuildcommands({
		"@echo Generating Wayland protocols...",
		-- Core Wayland & Shell
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/wayland.xml ../"
			.. raylib_dir
			.. "/src/wayland-client-protocol.h",
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/xdg-shell.xml ../"
			.. raylib_dir
			.. "/src/xdg-shell-client-protocol.h",
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/xdg-decoration-unstable-v1.xml ../"
			.. raylib_dir
			.. "/src/xdg-decoration-unstable-v1-client-protocol.h",

		-- Viewporter
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/viewporter.xml ../"
			.. raylib_dir
			.. "/src/viewporter-client-protocol.h",

		-- Relative Pointer
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/relative-pointer-unstable-v1.xml ../"
			.. raylib_dir
			.. "/src/relative-pointer-unstable-v1-client-protocol.h",
		-- Pointer Constraints
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/pointer-constraints-unstable-v1.xml ../"
			.. raylib_dir
			.. "/src/pointer-constraints-unstable-v1-client-protocol.h",

		-- Fractional Scale
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/fractional-scale-v1.xml ../"
			.. raylib_dir
			.. "/src/fractional-scale-v1-client-protocol.h",

		-- XDG Activation
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/xdg-activation-v1.xml ../"
			.. raylib_dir
			.. "/src/xdg-activation-v1-client-protocol.h",
		-- Idle Inhibit
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/idle-inhibit-unstable-v1.xml ../"
			.. raylib_dir
			.. "/src/idle-inhibit-unstable-v1-client-protocol.h",
	})
	filter({})

	filter("action:vs*")
	defines({ "_WINSOCK_DEPRECATED_NO_WARNINGS", "_CRT_SECURE_NO_WARNINGS" })
	characterset("Unicode")
	buildoptions({ "/Zc:__cplusplus" })
	filter({})

	includedirs({ raylib_dir .. "/src", raylib_dir .. "/src/external/glfw/include" })
	vpaths({
		["Header Files"] = { raylib_dir .. "/src/**.h" },
		["Source Files/*"] = { raylib_dir .. "/src/**.c" },
	})
	files({ raylib_dir .. "/src/*.h", raylib_dir .. "/src/*.c" })

	removefiles({ raylib_dir .. "/src/rcore_*.c" })

	filter({ "system:macosx" })
	toolset("clang")
	buildoptions({ "-x objective-c" })
	links({
		"OpenGL.framework",
		"Cocoa.framework",
		"IOKit.framework",
		"CoreFoundation.framework",
		"CoreAudio.framework",
		"CoreVideo.framework",
		"AudioToolbox.framework",
		"QuartzCore.framework",
	})

	filter({ "system:windows", "action:gmake*" })
	buildoptions({ "-Winvalid-pch" })

	apply_perf_flags()

	filter({})
end

-- Unit test executable. Deliberately does NOT link raylib: every module under
-- test must stay headless (raylib.h / raymath.h types are fine to include,
-- but no RLAPI call may be reached from a test translation unit). This keeps
-- `./bin/Release/referentia-tests` runnable on CI with no GPU, no display and
-- no X11 server.
if not _OPTIONS["with-emscripten"] then
	project("referentia-tests")
	kind("ConsoleApp")
	language("C++")
	location("build_files/")
	targetdir("../bin/%{cfg.buildcfg}")
	targetname("referentia-tests")
	cppdialect("C++17")
	optimize("On")

	files({ "../src/tests/**.cpp", "../src/tests/**.h" })

	includedirs({ "../src", "../include", raylib_dir .. "/src" })

	defines({ "RUN_TESTS" })
	buildoptions({ "-Wshadow" })

	filter({ "toolset:clang or gcc" })
	linkoptions({ "-pthread" })

	filter({})
end
