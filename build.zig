const std = @import("std");

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});

    const wasm = b.addSystemCommand(&.{
        b.graph.zig_exe,
        "cc",
        "--target=wasm32-wasi",
        "-lc",
        "-mexec-model=reactor",
        "-fvisibility=hidden",
    });

    switch (optimize) {
        .Debug => wasm.addArg("-Og"),
        .ReleaseSafe, .ReleaseFast => wasm.addArg("-O2"),
        .ReleaseSmall => wasm.addArg("-Os"),
    }

    wasm.addArg("-o");
    const output = wasm.addOutputFileArg("tiff-predictor-2.wasm");
    wasm.addFileArg(b.path("tiff_predictor_2.c"));

    b.getInstallStep().dependOn(&b.addInstallFile(output, "tiff-predictor-2.wasm").step);

    // Native test binary (not Wasm) — run with `zig build test`
    const test_mod = b.createModule(.{
        .target = b.graph.host,
        .link_libc = true,
    });
    test_mod.addCSourceFile(.{ .file = b.path("test_tiff_predictor_2.c") });
    const test_exe = b.addExecutable(.{
        .name = "test_tiff_predictor_2",
        .root_module = test_mod,
    });

    const run_test = b.addRunArtifact(test_exe);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_test.step);
}
