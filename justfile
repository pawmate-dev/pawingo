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
    python -m unittest discover -s tests -p test_release.py
    cargo test --manifest-path native/fsrs/Cargo.toml --target-dir build/fsrs --locked
    xmake test-suite

clean:
    xmake clean

fmt:
    cargo fmt --manifest-path native/fsrs/Cargo.toml
    clang-format -i $(rg --files src modules plugins tests -g '*.c' -g '*.cc' -g '*.cpp' -g '*.cxx' -g '*.h' -g '*.hh' -g '*.hpp' -g '*.hxx' -g '*.cppm' -g '*.ixx')
    rg --null --files -g '*.md' -g '!README*' | xargs -0 -r prettier --write --editorconfig --prose-wrap always

# Build the plugin first; only the host embeds the final checksum-pinned catalog.
[positional-arguments]
release tag:
    PAWINGO_CATALOG_SOURCE=resources/plugins.yaml just build
    just test
    xmake release-paths
    python tools/release.py plugin --tag "$1"
    PAWINGO_CATALOG_SOURCE=build/release/plugins-v1.yaml xmake build pawingo
    python tools/release.py core --tag "$1"
