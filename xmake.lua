set_project("pawingo")
set_version("0.2.0")
set_languages("cxx26")
add_rules("mode.debug", "mode.release")
add_requires("ftxui 6.1.9")
add_requires("sqlite_orm v1.9.1")
add_requires("expat 2.7.3", "zlib 1.3.1")
add_requires("libcurl >=8.0", "picosha2 2022.08.08")
add_requires("miniaudio 0.11.25", "nlohmann_json 3.12.0")
add_requires("libarchive")
add_requires("yaml-cpp >=0.8")


-- Embed the maintained SQL files so installed binaries do not need a working directory.
local function embed_sql(target)
  local output_dir = path.join(os.projectdir(), "build", "generated")
  local output = path.join(output_dir, "pawingo_sql.hpp")
  local parts = {"#pragma once\nnamespace pawingo::sql {\n"}
  local files = os.files(path.join(os.projectdir(), "sql", "*.sql"))
  table.sort(files)
  for _, file in ipairs(files) do
    local name = path.basename(file)
    local sql = io.readfile(file)
    assert(not sql:find(")pawingo_sql\"", 1, true), "SQL contains raw string delimiter")
    table.insert(parts, "inline constexpr char " .. name .. "[] = R\"pawingo_sql(" .. sql .. ")pawingo_sql\";\n")
  end
  table.insert(parts, "}\n")
  local content = table.concat(parts)
  os.mkdir(output_dir)
  if not os.isfile(output) or io.readfile(output) ~= content then
    io.writefile(output, content)
  end
  target:add("includedirs", output_dir)
end

-- Rewrite generated headers only when data contents change; compiler dependencies handle rebuilds.
-- Hook-local APIs must be passed explicitly: ordinary Lua helpers do not inherit xmake's hook sandbox.
local function embed_assets(target, namespace_name, header_name, entries, context)
  local io, os, path, assert, table, ipairs = context.io, context.os, context.path, context.assert, context.table, context.ipairs
  local output_dir = path.join(os.projectdir(), "build", "generated")
  local parts = {"#pragma once\nnamespace " .. namespace_name .. " {\n"}
  for _, entry in ipairs(entries) do
    local content = io.readfile(path.join(os.projectdir(), entry[2]))
    assert(not content:find(")pawingo_asset\"", 1, true), "Resource contains raw string delimiter")
    table.insert(parts, "inline constexpr char " .. entry[1] .. "[] = R\"pawingo_asset(" .. content .. ")pawingo_asset\";\n")
  end
  table.insert(parts, "}\n")
  local output = path.join(output_dir, header_name)
  local content = table.concat(parts)
  os.mkdir(output_dir)
  if not os.isfile(output) or io.readfile(output) ~= content then io.writefile(output, content) end
  target:add("includedirs", output_dir)
end

local function embed_voicevox(target)
  local entries = {}
  for _, item in ipairs({{"voicevox_manifest", "voicevox.yaml"}, {"voicevox_runtime_terms", "voicevox-runtime-terms.txt"},
                         {"japanese_defaults", "defaults.yaml"}, {"japanese_kana", "kana.yaml"}, {"japanese_messages", "messages.yaml"}}) do
    table.insert(entries, {item[1], path.join("plugins", "japanese", "resources", item[2])})
  end
  embed_assets(target, "pawingo::japanese::assets", "pawingo_voicevox_assets.hpp", entries, {io=io, os=os, path=path, assert=assert, table=table, ipairs=ipairs})
end

local function embed_application(target)
  embed_assets(target, "pawingo::config::assets", "pawingo_application_assets.hpp",
               {{"application", "resources/application.yaml"}}, {io=io, os=os, path=path, assert=assert, table=table, ipairs=ipairs})
end

local function common()
  add_deps("pawingo-config")
  add_includedirs("include")
  add_packages("ftxui")
end

target("pawingo-config")
  set_kind("static")
  on_load(embed_application)
  add_includedirs("include", {public = true})
  add_packages("yaml-cpp", {public = true})
  add_files("src/settings.cpp")
  if is_plat("linux", "macosx") then add_cxflags("-fPIC") end

target("pawingo")
  on_load(embed_sql)
  set_kind("binary")
  common()
  add_files("src/main.cpp", "src/storage/store.cpp", "src/plugins/registry.cpp", "src/learning/search.cpp", "src/review.cppm")
  add_packages("sqlite_orm")
  add_syslinks("sqlite3", "dl")

target("test-plugin")
  set_kind("shared")
  set_default(false)
  set_targetdir("build/test-plugins")
  common()
  add_files("tests/fixtures/plugin.cpp")

target("pawingo-tests")
  on_load(embed_sql)
  set_kind("binary")
  set_default(false)
  common()
  add_files("tests/core.cpp", "src/storage/store.cpp", "src/plugins/registry.cpp", "src/learning/search.cpp", "src/review.cppm")
  add_packages("sqlite_orm")
  add_syslinks("sqlite3", "dl")
  add_deps("test-plugin")

target("japanese")
  on_load(embed_voicevox)
  set_kind("shared")
  set_default(false)
  set_targetdir("build/plugins")
  common()
  add_files("plugins/japanese/plugin.cpp", "plugins/japanese/dictionary.cpp", "plugins/japanese/search_panel.cpp", "plugins/japanese/voicevox.cpp", "plugins/japanese/settings.cpp", "plugins/japanese/voicevox_panel.cpp", "plugins/japanese/voicevox_install.cpp", "plugins/japanese/voicevox_manifest.cpp", "src/download.cpp")
  add_includedirs("third_party/voicevox")
  add_packages("miniaudio", "nlohmann_json", "libarchive", "yaml-cpp")
  if is_plat("linux") then add_syslinks("dl", "pthread", "m") end
  add_packages("expat", "zlib", "libcurl", "picosha2")

target("japanese-tests")
  set_kind("binary")
  set_default(false)
  common()
  add_files("tests/japanese.cpp")
  add_syslinks("dl")
  add_deps("japanese")

target("japanese-dictionary-tests")
  on_load(embed_voicevox)
  set_kind("binary")
  set_default(false)
  common()
  add_files("tests/japanese_dictionary.cpp", "plugins/japanese/dictionary.cpp", "plugins/japanese/settings.cpp")
  add_packages("expat", "zlib")

target("download-tests")
  add_deps("pawingo-config")
  set_kind("binary")
  set_default(false)
  add_includedirs("include")
  add_files("tests/download.cpp", "src/download.cpp")
  add_packages("libcurl", "picosha2")

target("background-job-tests")
  set_kind("binary")
  set_default(false)
  add_includedirs("include")
  add_files("tests/background_job.cpp")

target("voicevox-tests")
  on_load(embed_voicevox)
  add_deps("pawingo-config")
  set_kind("binary")
  set_default(false)
  add_includedirs("include", "third_party/voicevox")
  add_files("tests/voicevox.cpp", "plugins/japanese/voicevox.cpp", "plugins/japanese/settings.cpp")
  add_packages("miniaudio", "nlohmann_json")
  if is_plat("linux") then add_syslinks("dl", "pthread", "m") end

target("voicevox-install-tests")
  add_deps("pawingo-config")
  on_load(embed_voicevox)
  set_kind("binary")
  set_default(false)
  add_includedirs("include")
  add_files("tests/voicevox_install.cpp", "plugins/japanese/voicevox_install.cpp", "plugins/japanese/voicevox_manifest.cpp", "plugins/japanese/settings.cpp", "src/download.cpp")
  add_packages("libarchive", "libcurl", "picosha2", "yaml-cpp")

target("settings-tests")
  set_kind("binary")
  set_default(false)
  add_deps("pawingo-config")
  add_includedirs("include")
  add_files("tests/settings.cpp")
