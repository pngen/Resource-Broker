# Contributing to Resource Broker

Thank you for contributing to Resource Broker. Resource Broker is a
vendor-neutral, open-source C++20 runtime that provides cross-resource
reservation, arbitration, and authoritative accounting for scarce AI
infrastructure resources.

## Development environment

Resource Broker targets C++20 and builds with CMake plus Ninja. The
reference validation environment is Windows with MSVC 19.44 or later.
CUDA is optional; ordinary consumers build without it.

## Building

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Build options (see CMakeLists.txt):

- `RESOURCEBROKER_BUILD_TESTS` (default ON)
- `RESOURCEBROKER_BUILD_EXAMPLES` (default ON)
- `RESOURCEBROKER_BUILD_BENCHMARKS` (default OFF)
- `RESOURCEBROKER_ENABLE_SYNTHETIC_RESOURCES` (default ON)
- `RESOURCEBROKER_ENABLE_CUDA_PROOF` (default OFF; enable when a CUDA
  device is available)

## Code style

- C++20, strong types, deterministic behavior, exact accounting.
- Zero warnings under `/W4 /WX`.
- Concrete resource facts are never fabricated: unknown facts remain
  UNKNOWN and unavailable infrastructure is represented with explicit
  SYNTHETIC provenance.

## Tests

Run `ctest --test-dir build --output-on-failure` after building.
Property tests use fixed seeds and print the seed used.

## License and attribution

Resource Broker is licensed under the Apache License 2.0. Copyright 2026
Summon Software Labs. By contributing you agree that your contribution
is licensed under the same terms.
