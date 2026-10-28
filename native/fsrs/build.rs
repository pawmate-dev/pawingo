//! Generates the private C ABI declarations from their Rust source of truth.

use std::{env, error::Error, fs, path::PathBuf};

fn main() -> Result<(), Box<dyn Error>> {
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);
    let output = root.join("include/pawingo_fsrs.h");
    fs::create_dir_all(output.parent().ok_or("Missing header directory")?)?;
    let bindings = cbindgen::Builder::new()
        .with_crate(&root)
        .with_config(cbindgen::Config::from_file(root.join("cbindgen.toml"))?)
        .generate()?;
    bindings.write_to_file(output);
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=cbindgen.toml");
    Ok(())
}
