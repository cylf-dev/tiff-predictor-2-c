# Changelog

All notable changes to this project will be documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [v0.3.0] - 2025-04-01

### Added

- Vendored `core_abi.h` and `core_abi.c` from chonkle for port-map serialization/deserialization.
- Embedded `chonkle:signature` custom section in the Wasm binary, declaring codec metadata (identifier, implementation name, input/output port types).

### Changed

- Switched from the 4-parameter ABI (`encode(input_ptr, input_len, config_ptr, config_len)`) to the chonkle core ABI port-map wire format (`encode(port_map_ptr, port_map_len)`). All input ports (`bytes`, `bytes_per_sample`, `width`) are now serialized into a single binary port-map buffer. Output is also a serialized port-map.

## [v0.2.0] - 2025-03-05

### Changed

- Wasm file now includes a `-c` suffix in its name to indicate that it is compiled from C code. This differentiates it from other tiff-predictor-2 Wasm files that are compiled from different source languages.

## [v0.1.0] - 2025-02-26

Initial release.

[Unreleased]: https://github.com/cylf-dev/tiff-predictor-2-c/compare/v0.3.0...main
[v0.3.0]: https://github.com/cylf-dev/tiff-predictor-2-c/releases/tag/v0.3.0
[v0.2.0]: https://github.com/cylf-dev/tiff-predictor-2-c/releases/tag/v0.2.0
[v0.1.0]: https://github.com/cylf-dev/tiff-predictor-2-c/releases/tag/v0.1.0
