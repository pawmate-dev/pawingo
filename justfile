set shell := ["bash", "-cu"]

default:
    @just --list

build:
    xmake build pawingo japanese

run: build
    PAWINGO_PLUGIN_DIR="$(pwd)/build/plugins" xmake run pawingo

check-modules:
    xmake check-modules

test: check-modules
    cargo test --manifest-path native/fsrs/Cargo.toml --target-dir build/fsrs --locked
    xmake build pawingo-tests japanese-tests japanese-dictionary-tests japanese-examples-tests japanese-search-tests download-tests background-job-tests diagnostics-tests review-tests study-tests voicevox-tests voicevox-install-tests settings-tests
    xmake run pawingo-tests "$(pwd)/build/test-plugins/libtest-plugin.so"
    xmake run japanese-tests "$(pwd)/build/plugins/libjapanese.so" "$(pwd)/tests/fixtures/jmdict.xml"
    xmake run japanese-dictionary-tests "$(pwd)/tests/fixtures/jmdict.xml"
    xmake run japanese-examples-tests
    xmake run japanese-search-tests "$(pwd)/tests/fixtures/jmdict.xml"
    xmake run download-tests
    xmake run background-job-tests
    xmake run diagnostics-tests
    xmake run review-tests
    xmake run study-tests
    xmake run voicevox-tests "$(pwd)/build/test-plugins/libvoicevox-test-runtime.so"
    xmake run voicevox-install-tests
    xmake run settings-tests

clean:
    xmake clean

fmt:
    cargo fmt --manifest-path native/fsrs/Cargo.toml
    clang-format -i $(rg --files src modules plugins tests -g '*.c' -g '*.cc' -g '*.cpp' -g '*.cxx' -g '*.h' -g '*.hh' -g '*.hpp' -g '*.hxx' -g '*.cppm' -g '*.ixx')
    rg --null --files -g '*.md' -g '!README*' | xargs -0 -r prettier --write --editorconfig --prose-wrap always
