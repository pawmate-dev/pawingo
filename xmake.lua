set_project("pawingo")
local pawingo_version = "0.1.0"
set_version(pawingo_version)
set_languages("cxx26")
set_policy("build.c++.modules", true)
add_rules("mode.debug", "mode.release")
if is_plat("windows") then
  add_cxflags("/utf-8")
  add_defines("NOMINMAX")
end
add_requires("ftxui 6.1.9", {system = false})
add_requires("sqlite3")
add_requires("sqlite_orm v1.9.1")
add_requires("expat 2.7.3", "zlib 1.3.1")
add_requires("libcurl >=8.0", "picosha2 2022.08.08")
add_requires("miniaudio 0.11.25", "nlohmann_json 3.12.0")
add_requires("libarchive")
add_requires("yaml-cpp >=0.8", {system = false})


-- Embed the maintained SQL files so installed binaries do not need a working directory.
local function embed_sql(target, context)
  local io, os, path, assert, table, ipairs = context.io, context.os, context.path, context.assert, context.table, context.ipairs
  local output_dir = path.join(os.projectdir(), "build", "generated")
  local output = path.join(output_dir, "pawingo_sql.cppm")
  local parts = {"export module pawingo.assets.sql;\nexport namespace pawingo::sql {\n"}
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
  target:add("files", output)
end

-- Rewrite generated headers only when data contents change; compiler dependencies handle rebuilds.
-- Hook-local APIs must be passed explicitly: ordinary Lua helpers do not inherit xmake's hook sandbox.
local function embed_assets(target, namespace_name, module_name, header_name, entries, context)
  local io, os, path, assert, table, ipairs = context.io, context.os, context.path, context.assert, context.table, context.ipairs
  local output_dir = path.join(os.projectdir(), "build", "generated")
  local parts = {"export module " .. module_name .. ";\nexport namespace " .. namespace_name .. " {\n"}
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
  target:add("files", output)
end

local function embed_voicevox(target, context)
  local path, table, ipairs = context.path, context.table, context.ipairs
  local entries = {}
  for _, item in ipairs({{"voicevox_manifest", "voicevox.yaml"}, {"voicevox_runtime_terms", "voicevox-runtime-terms.txt"},
                         {"japanese_defaults", "defaults.yaml"}, {"japanese_kana", "kana.yaml"}, {"japanese_messages", "messages.yaml"}}) do
    table.insert(entries, {item[1], path.join("plugins", "japanese", "resources", item[2])})
  end
  table.insert(entries, {"japanese_examples_sql", path.join("plugins", "japanese", "sql", "examples.sql")})
  embed_assets(target, "pawingo::japanese::assets", "pawingo.japanese.assets", "pawingo_voicevox_assets.cppm", entries, context)
end

local function embed_application(target, context)
  embed_assets(target, "pawingo::config::assets", "pawingo.assets.application", "pawingo_application_assets.cppm",
               {{"application", "resources/application.yaml"}}, context)
end

-- Cargo owns the Rust graph; xmake links one PIC static adapter into its normal target graph.
target("pawingo-fsrs")
  set_kind("static")
  set_default(false)
  on_load(function (target)
    -- A Cargo-built target has no xmake source files, so advertise its archive explicitly.
    target:add("linkdirs", target:targetdir(), {public = true})
    target:add("links", "pawingo-fsrs", {public = true})
  end)
  on_build(function (target)
    import("core.project.depend")
    import("core.project.config")
    local arch = ({x86_64="x86_64", x64="x86_64", arm64="aarch64", aarch64="aarch64"})[target:arch()]
    local suffix = ({linux="unknown-linux-gnu", macosx="apple-darwin", windows="pc-windows-msvc"})[target:plat()]
    assert(arch and suffix, "Unsupported Rust FSRS target: " .. target:plat() .. "/" .. target:arch())
    local triple = arch .. "-" .. suffix
    local profile = config.get("mode") == "debug" and "debug" or "release"
    local cargo_dir = path.join(os.projectdir(), "build", "fsrs")
    local source = path.join(os.projectdir(), "native", "fsrs")
    local files = os.files(path.join(source, "**.rs"))
    table.insert(files, path.join(source, "Cargo.toml"))
    table.insert(files, path.join(source, "Cargo.lock"))
    table.insert(files, path.join(source, "cbindgen.toml"))
    depend.on_changed(function ()
      local args = {"build", "--locked", "--manifest-path", path.join(source, "Cargo.toml"),
                    "--target-dir", cargo_dir, "--target", triple}
      if profile == "release" then table.insert(args, "--release") end
      os.vrunv("cargo", args, {envs={RUSTFLAGS=(os.getenv("RUSTFLAGS") or "") .. " -C relocation-model=pic"}})
      local archive = target:is_plat("windows") and "pawingo_fsrs_ffi.lib" or "libpawingo_fsrs_ffi.a"
      os.cp(path.join(cargo_dir, triple, profile, archive), target:targetfile())
    end, {dependfile=target:dependfile(target:targetfile()), files=files,
          values={triple, profile, os.getenv("RUSTFLAGS") or ""},
          changed=target:is_rebuilt() or not os.isfile(target:targetfile())})
  end)
  if is_plat("linux") then add_syslinks("pthread", "dl", "m", {public = true}) end
  if is_plat("macosx") then add_syslinks("iconv", "resolv", {public = true}) end
  if is_plat("windows") then add_syslinks("advapi32", "bcrypt", "ntdll", "userenv", "ws2_32", {public = true}) end

-- Shared module interfaces/BMIs are compiled once, then propagated through target dependencies.
target("pawingo-runtime")
  set_kind("static")
  add_deps("pawingo-fsrs", {public = true})
  add_includedirs("native/fsrs/include")
  on_load(function (target)
    local context = {io=io, os=os, path=path, assert=assert, table=table, ipairs=ipairs}
    embed_sql(target, context)
    embed_application(target, context)
    import("core.project.config")
    local identity = os.getenv("PAWINGO_RELEASE_BUILD") or "development"
    assert(identity:match("^[%w._-]+$") and #identity <= 128, "Invalid release build ID")
    target:add("defines", 'PAWINGO_HOST_VERSION="' .. pawingo_version .. '"', {public=true})
    target:add("defines", 'PAWINGO_RELEASE_BUILD="' .. identity .. '"', {public=true})
    target:add("defines", 'PAWINGO_BUILD_MODE="' .. (config.get("mode") or "release") .. '"', {public=true})
  end)
  add_files("modules/*.cppm", {public = true})
  add_files("src/settings.cpp", "src/ui/input.cpp", "src/ui/activity.cpp", "src/learning/activity.cpp", "src/io/diagnostics.cpp", "src/io/native.cpp", "src/review/scheduler.cpp", "src/review/panel.cpp", "src/download.cpp", "src/storage/store.cpp", "src/plugins/registry.cpp", "src/plugins/packages.cpp", "src/plugins/package_panel.cpp", "src/learning/search.cpp")
  add_packages("ftxui", "yaml-cpp", "sqlite3", "sqlite_orm", "libcurl", "picosha2", "libarchive", {public = true})
  if is_plat("linux") then add_syslinks("dl", "pthread", "m", {public = true}) end

  if is_plat("linux", "macosx") then add_cxflags("-fPIC") end

target("japanese-backend")
  set_kind("static")
  set_default(false)
  on_load(function (target)
    embed_voicevox(target, {io=io, os=os, path=path, assert=assert, table=table, ipairs=ipairs})
  end)
  add_deps("pawingo-runtime", {public = true})
  add_files("plugins/japanese/modules/*.cppm", {public = true})
  add_files("plugins/japanese/plugin.cpp", "plugins/japanese/dictionary.cpp", "plugins/japanese/review.cpp", "plugins/japanese/examples.cpp", "plugins/japanese/search_panel.cpp", "plugins/japanese/voicevox.cpp", "plugins/japanese/settings.cpp", "plugins/japanese/voicevox_panel.cpp", "plugins/japanese/voicevox_install.cpp", "plugins/japanese/voicevox_manifest.cpp")
  add_includedirs("third_party/voicevox")
  add_packages("miniaudio", "nlohmann_json", "libarchive", "expat", "zlib", {public = true})
  if is_plat("linux", "macosx") then add_cxflags("-fPIC") end

target("pawingo")
  set_kind("binary")
  add_deps("pawingo-runtime")
  on_load(function (target)
    local source = os.getenv("PAWINGO_CATALOG_SOURCE") or "resources/plugins.yaml"
    embed_assets(target, "pawingo::catalog_assets", "pawingo.assets.catalog", "pawingo_catalog_assets.cppm",
      {{"plugins", source}}, {io=io, os=os, path=path, assert=assert, table=table, ipairs=ipairs})
  end)
  add_files("src/main.cpp")

target("japanese")
  set_kind("shared")
  set_default(false)
  set_targetdir("build/plugins")
  add_deps("japanese-backend")
  add_files("plugins/japanese/entry.cpp")

target("test-plugin")
  set_kind("shared")
  set_default(false)
  set_targetdir("build/test-plugins")
  add_deps("pawingo-runtime")
  add_files("tests/fixtures/plugin.cppm", "tests/fixtures/plugin.cpp")

target("voicevox-test-runtime")
  set_kind("shared")
  set_default(false)
  set_targetdir("build/test-plugins")
  add_files("tests/fixtures/voicevox.cpp")
  add_includedirs("third_party/voicevox")
  set_symbols("debug")

-- Exercise standard-library modules and experimental link requirements before the main build.
target("toolchain-check")
  set_kind("binary")
  set_default(false)
  add_files("tests/toolchain.cpp")

target("pawingo-test-support")
  set_kind("static")
  set_default(false)
  add_files("tests/fixtures/platform.cppm", {public = true})

target("test-process")
  set_kind("binary")
  set_default(false)
  set_basename("espeak-ng")
  add_files("tests/fixtures/process.cpp")

local test_specs = {
  {"native-tests", "tests/native.cpp", "process"},
  {"pawingo-tests", "tests/core.cpp", "core"},
  {"japanese-tests", "tests/japanese.cpp", "native"},
  {"japanese-dictionary-tests", "tests/japanese_dictionary.cpp", "backend"},
  {"japanese-examples-tests", "tests/japanese_examples.cpp", "backend"},
  {"japanese-search-tests", "tests/japanese_search.cpp", "backend"},
  {"japanese-review-tests", "tests/japanese_review.cpp", "backend"},
  {"download-tests", "tests/download.cpp", "runtime"},
  {"background-job-tests", "tests/background_job.cpp", "runtime"},
  {"diagnostics-tests", "tests/diagnostics.cpp", "runtime"},
  {"review-tests", "tests/review.cpp", "runtime"},
  {"study-tests", "tests/study.cpp", "runtime"},
  {"activity-tests", "tests/activity.cpp", "runtime"},
  {"package-tests", "tests/packages.cpp", "core"},
  {"voicevox-tests", "tests/voicevox.cpp", "backend"},
  {"voicevox-install-tests", "tests/voicevox_install.cpp", "backend"},
  {"settings-tests", "tests/settings.cpp", "runtime"}
}
for _, spec in ipairs(test_specs) do
  target(spec[1])
    set_kind("binary")
    set_default(false)
    add_files(spec[2])
    add_deps("pawingo-runtime", "pawingo-test-support")
    if spec[3] == "backend" then add_deps("japanese-backend") end
    if spec[3] == "core" then add_deps("test-plugin", {inherit = false}) end
    if spec[3] == "process" then add_deps("test-process", {inherit = false}) end
    if spec[3] == "native" then add_deps("japanese", "test-process", {inherit = false}) end
    if spec[1] == "voicevox-tests" then add_deps("voicevox-test-runtime", {inherit = false}) end
end

-- Guard the module-only project boundary while allowing native/vendor preprocessor adapters.
task("check-modules")
  set_menu {usage = "xmake check-modules", description = "Verify named-module project boundaries", options = {}}
  on_run(function ()
    local files = {}
    for _, root in ipairs({"src", "modules", "plugins/japanese", "tests"}) do
      for _, pattern in ipairs({"/**.cpp", "/**.cppm"}) do
        for _, file in ipairs(os.files(root .. pattern)) do table.insert(files, file) end
      end
    end
    local entries = {["src/main.cpp"]=true, ["plugins/japanese/entry.cpp"]=true, ["tests/fixtures/plugin.cpp"]=true}
    for _, file in ipairs(files) do
      local source = "\n" .. io.readfile(file)
      for header in source:gmatch('#include%s*[<"]([^>"]+)[>"]') do
        assert(not header:find("^pawingo/"), file .. ": project interfaces must use import")
        if not header:find("[/.]") then
          assert(header == "cerrno" or header == "csignal" or header == "cassert" or header == "version",
                 file .. ": standard-library facilities must use import std, not " .. header)
        end
      end
      for header in source:gmatch('#include%s+"([^"]+)"') do
        assert(header == "voicevox_core.h" or header == "pawingo_fsrs.h", file .. ": project interfaces must use import, not #include " .. header)
      end
      if path.extension(file) == ".cppm" then
        assert(source:find("\n%s*export%s+module%s+[%w_.]+%s*;"), file .. ": missing exported module declaration")
      elseif not entries[file] and not file:find("^tests/") then
        assert(source:find("\n%s*module%s+[%w_.]+%s*;"), file .. ": missing implementation module declaration")
      end
    end
    assert(#os.files("include/pawingo/**.hpp") == 0, "Project public APIs belong in named modules")
    assert(#os.files("plugins/japanese/*.hpp") == 0, "Japanese interfaces belong in named modules")
    print("Module boundaries verified for %d source files", #files)
  end)

-- Resolve actual target filenames instead of assuming ELF suffixes or a particular architecture.
task("test-suite")
  set_menu {usage = "xmake test-suite", description = "Build and run native integration tests", options = {}}
  on_run(function ()
    import("core.project.config")
    config.load()
    import("core.project.project")
    local targets = {}
    for _, spec in ipairs(test_specs) do table.insert(targets, spec[1]) end
    local build_args = {"build"}
    for _, name in ipairs(targets) do table.insert(build_args, name) end
    os.vrunv(os.programfile(), build_args)
    local function file(name) return path.absolute(project.target(name):targetfile()) end
    local fixture = path.join(os.projectdir(), "tests", "fixtures", "jmdict.xml")
    local args = {
      ["native-tests"] = {file("test-process")},
      ["pawingo-tests"] = {file("test-plugin")},
      ["japanese-tests"] = {file("japanese"), fixture, file("test-process")},
      ["japanese-dictionary-tests"] = {fixture},
      ["japanese-search-tests"] = {fixture},
      ["japanese-review-tests"] = {os.projectdir()},
      ["package-tests"] = {file("test-plugin")},
      ["voicevox-tests"] = {file("voicevox-test-runtime")}
    }
    for _, spec in ipairs(test_specs) do
      local run_args = {"run", spec[1]}
      for _, arg in ipairs(args[spec[1]] or {}) do table.insert(run_args, arg) end
      os.vrunv(os.programfile(), run_args)
      print("%s passed", spec[1])
    end
  end)

task("release-paths")
  set_menu {usage = "xmake release-paths", description = "Write native release input paths", options = {}}
  on_run(function ()
    import("core.project.config")
    config.load()
    import("core.project.project")
    import("core.base.json")
    os.mkdir("build")
    json.savefile("build/release-paths.json", {
      binary = path.absolute(project.target("pawingo"):targetfile()),
      library = path.absolute(project.target("japanese"):targetfile())
    })
  end)
