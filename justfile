set shell := ["bash", "-cu"]

default:
    @just --list

build:
    xmake build pawingo japanese

run: build
    PAWINGO_PLUGIN_DIR="$(pwd)/build/plugins" xmake run pawingo

test:
    xmake build pawingo-tests japanese-tests
    xmake run pawingo-tests "$(pwd)/build/test-plugins/libtest-plugin.so"
    xmake run japanese-tests "$(pwd)/build/plugins/libjapanese.so"

clean:
    xmake clean

fmt:
    clang-format -i $(rg --files -g '*.c' -g '*.cc' -g '*.cpp' -g '*.cxx' -g '*.h' -g '*.hh' -g '*.hpp' -g '*.hxx' -g '*.cppm' -g '*.ixx')
    rg --null --files -g '*.md' -g '!README*' | xargs -0 -r prettier --write --editorconfig --prose-wrap always
