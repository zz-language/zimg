fn main() {
    let vips = pkg_config::probe_library("vips").expect("libvips not found via pkg-config");
    let mut build = cc::Build::new();
    build.file("../csrc/zimg_wrapper.c");
    for inc in &vips.include_paths {
        build.include(inc);
    }
    // pkg-config probe_library already emits cargo:rustc-link-lib directives
    build.compile("zimg_wrapper");
}
