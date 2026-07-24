# Dependency Management

This document explains how dependencies are versioned and updated in fin-kit.

> **Setup rule:** QuantLib is intentionally built from source via
> `./scripts/build-quantlib.sh`. Do **not** use `brew install quantlib` (or a
> system package) for fin-kit.

## Source of Truth

- **Environment setup / install order:** [SETUP.md](SETUP.md)
- **Local runtime services:** [LOCAL_SERVICES.md](LOCAL_SERVICES.md)
- **Dependency versions:** `conanfile.py`, `scripts/build-quantlib.sh`,
  `CMakeLists.txt`, and `pyproject.toml`

## Version Sources

| Dependency | File | Format | Datasource |
|------------|------|--------|------------|
| Conan packages | `conanfile.py` | `self.requires("pkg/x.y.z")` | Conan Center |
| QuantLib | `scripts/build-quantlib.sh` | `QUANTLIB_VERSION:-x.y` | GitHub Releases |
| GoogleTest | `CMakeLists.txt` | `GIT_TAG vx.y.z` | GitHub Tags |
| Python tools | `pyproject.toml` | Standard | PyPI |

## Renovatebot Configuration

Renovatebot is configured via `renovate.json` to automatically create PRs when dependencies have updates.

### How it works

1. **Conan packages** (libpqxx, redis-plus-plus, spdlog, nlohmann_json, tomlplusplus, eigen)
   - Regex matches `self.requires("name/version")` in `conanfile.py`
   - Checks Conan Center for new versions

2. **QuantLib**
   - Regex matches `QUANTLIB_VERSION:-x.y` in `scripts/build-quantlib.sh`
   - Checks GitHub releases for `lballabio/QuantLib`

3. **GoogleTest**
   - Regex matches `GIT_TAG vx.y.z` in root `CMakeLists.txt`
   - Checks GitHub tags for `google/googletest`

4. **Python dependencies**
   - Native support via `pyproject.toml`
   - Checks PyPI for conan, pre-commit, etc.

### Grouped PRs

C++ dependencies are grouped into a single PR to reduce noise. Python dependencies come in separate PRs.

## Manual Updates

### Conan packages

Edit `conanfile.py`:
```python
self.requires("libpqxx/7.10.0")  # Change version here
```

Then rebuild:
```bash
uv run conan install . --build=missing -of=build ...
```

Note: `fmt` is pulled in transitively by `spdlog`, so the direct source of
truth is `conanfile.py`, not this prose.

### QuantLib

Set the version and rebuild:
```bash
QUANTLIB_VERSION=1.41 ./scripts/build-quantlib.sh
```

Or edit the default in `scripts/build-quantlib.sh`:
```bash
QUANTLIB_VERSION="${QUANTLIB_VERSION:-1.41}"
```

### GoogleTest

Edit `CMakeLists.txt`:
```cmake
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.0)  # Change version here
```

## Testing Renovatebot Locally

```bash
# Install renovate CLI
npm install -g renovate

# Dry run (no PRs created)
LOG_LEVEL=debug renovate --dry-run --platform=local
```

## Regex Pattern Reference

If file formats change, update `renovate.json`. Test patterns at https://regex101.com/.

| Pattern | Matches |
|---------|---------|
| `self\.requires\("(?<depName>[^/]+)/(?<currentValue>[^"]+)"\)` | `self.requires("libpqxx/7.9.2")` |
| `QUANTLIB_VERSION="\$\{QUANTLIB_VERSION:-(?<currentValue>[^}]+)\}` | `QUANTLIB_VERSION:-1.40` |
| `GIT_TAG\s+(?<currentValue>v[\d.]+)` | `GIT_TAG v1.14.0` |
