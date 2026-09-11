ZeroSlack Test Fixtures
=======================

Purpose
-------

This directory contains first-party SystemVerilog fixtures and headless
regression tests for semantic extraction, completion, navigation, relationship
analysis, RTL insight services, and real workspace behavior.

Current fixture policy
----------------------

- Tests use semantic-native records and `SemanticFixtureRecordBuilder` helpers.
- The old fixture-only `sym_list` / `syminfo` carrier has been deleted.
- Reverse adapters that convert semantic records back into legacy symbol
  carriers are not allowed.
- The final zero target scans first-party repo source, including tracked test
  fixtures, while excluding docs and guard definitions.

Important files
---------------

- `test_symbols.sv`: Compact fixture covering modules, ports, signals,
  parameters, typedefs, enums, structs, tasks, functions, and instances.
- `semantic_fixture_records.h`: Native helper builders for semantic test
  records, owners, type references, local handles, stable keys, and
  relationship endpoints.
- `completion_test.cpp`: Headless completion service regression coverage.
- `jump_test.cpp`: Headless definition/navigation regression coverage.
- `relationship_test.cpp`: Relationship, diagnostics, hierarchy, references,
  RTL insight service, semantic diff, and real workspace regression coverage.
- `gui_smoke_test.cpp`: Offscreen GUI smoke coverage that renders service/model
  output without direct UI semantic analysis.
- `large_file_perf_test.cpp`: Large-file responsiveness and performance smoke
  coverage.
- `new/`: Real workspace fixture used for package/import, include, interface,
  FSM, clock/reset, signal journey, module brief, and semantic diff coverage.

Build and run
-------------

Use the normal Qt 6 + CMake + Ninja build. From the configured build
directory, run:

```text
cmake --build . --target completion_test jump_test relationship_test gui_smoke_test large_file_perf_test
ctest --output-on-failure
```

Useful focused runs:

```text
ctest -R "completion_test|jump_test|relationship_test|gui_smoke_test" --output-on-failure
cmake -DZEROSLACK_SOURCE_DIR=<repo> -DZEROSLACK_PHASE_J_ZERO_TARGET=ON -P <repo>/cmake/legacy_field_policy_guard.ctest
```

Notes
-----

- GUI smoke tests run offscreen through CTest.
- Generated binaries are build artifacts and must not be checked in.
- New test fixtures should stay semantic-native and keep the Phase J zero
  target passing.
