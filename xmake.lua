set_project("pawingo")
set_version("0.2.0")
set_languages("cxx26")
add_rules("mode.debug", "mode.release")
add_requires("ftxui 6.1.9")
add_requires("sqlite_orm v1.9.1")


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

local function common()
  add_includedirs("include")
  add_packages("ftxui")
end

target("pawingo")
  on_load(embed_sql)
  set_kind("binary")
  common()
  add_files("src/main.cpp", "src/core.cpp", "src/review.cppm")
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
  add_files("tests/core.cpp", "src/core.cpp", "src/review.cppm")
  add_packages("sqlite_orm")
  add_syslinks("sqlite3", "dl")
  add_deps("test-plugin")
