set shell := ["bash", "-cu"]

default:
    @just --list

build:
    xmake build pawingo

run: build
    xmake run pawingo

test:
    xmake build pawingo-tests
    xmake run pawingo-tests "$(pwd)/build/test-plugins/libtest-plugin.so"

clean:
    xmake clean

fmt:
    clang-format -i $(rg --files -g '*.c' -g '*.cc' -g '*.cpp' -g '*.cxx' -g '*.h' -g '*.hh' -g '*.hpp' -g '*.hxx' -g '*.cppm' -g '*.ixx')
