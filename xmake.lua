set_project("pawingo")
set_version("0.1.0")
set_languages("cxx26")
add_rules("mode.debug", "mode.release")

add_requires("ftxui 6.1.9")

local modules = {"src/lesson.cppm", "src/japanese.cppm", "src/german.cppm"}

target("pawingo")
  set_kind("binary")
  add_files(modules)
  add_files("src/main.cpp")
  add_packages("ftxui")

target("pawingo-tests")
  set_kind("binary")
  set_default(false)
  add_files(modules)
  add_files("tests/lessons.cpp")
