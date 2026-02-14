# service-mvp

[![CI](https://github.com/LLevella/service-mvp/actions/workflows/ci.yml/badge.svg)](https://github.com/LLevella/service-mvp/actions/workflows/ci.yml)
[![Release](https://github.com/LLevella/service-mvp/actions/workflows/release.yml/badge.svg)](https://github.com/LLevella/service-mvp/actions/workflows/release.yml)

`service-mvp` is a small C utility for safe file maintenance tasks. It scans
explicitly configured directories, matches files by glob patterns, creates marker
files, and can remove or quarantine matched files.

The tool is intentionally conservative: it runs in `dry-run` mode by default and
does not change files unless `--apply` is passed.

## Features

- Recursive directory scanning.
- Separate directory lists for marking and removal/quarantine.
- Configurable glob patterns for both actions.
- Safe default mode: preview first, mutate only with `--apply`.
- Optional quarantine directory to move matched files instead of deleting them.
- One-shot or repeated scan passes with configurable interval.
- Human-readable run summary and optional syslog output.
- Smoke tests for the main dry-run and apply workflows.

## Requirements

- Linux or another POSIX-like environment.
- `make`.
- A C11 compiler such as `gcc` or `clang`.
- Standard C/POSIX runtime libraries.

## Build

```sh
make
```

The compiled binary is created as `./service-mvp`.

To remove build artifacts:

```sh
make clean
```

## Test

```sh
make check
```

The smoke test creates a temporary directory, checks that dry-run mode does not
modify files, then verifies marker creation and quarantine behavior in apply mode.

## Quick Start

Preview marker creation for matching PDF files:

```sh
./service-mvp --mark-dir ./sample --once
```

Preview quarantine candidates:

```sh
./service-mvp \
  --remove-dir ./sample \
  --quarantine-dir ./quarantine \
  --once
```

Apply quarantine only after the preview looks correct:

```sh
./service-mvp \
  --remove-dir ./sample \
  --quarantine-dir ./quarantine \
  --apply \
  --once
```

Run continuously every 5 minutes:

```sh
./service-mvp \
  --mark-dir ./sample \
  --passes 0 \
  --interval 300
```

## Safety Model

`service-mvp` has two operating modes:

- `dry-run`: default mode. The tool logs what it would do and exits without
  writing, moving, or deleting files.
- `apply`: enabled with `--apply`. The tool performs marker creation,
  quarantine moves, or direct removals.

Prefer `--quarantine-dir` for removal workflows. Without `--quarantine-dir`,
matched files are deleted with `unlink()` when `--apply` is used.

The scanner uses `lstat()` while walking directories and only processes regular
files. Directories are scanned recursively; symbolic links and special files are
skipped.

## Command Line Options

| Option | Description |
| --- | --- |
| `--mark-dir PATH` | Recursively scan `PATH` and create marker files for mark-pattern matches. Can be passed multiple times. |
| `--remove-dir PATH` | Recursively scan `PATH` and remove or quarantine remove-pattern matches. Can be passed multiple times. |
| `--mark-pattern GLOB` | Add a marker glob. Default: `test*.pdf`. |
| `--remove-pattern GLOB` | Add a removal glob. Defaults: `test*.doc`, `test*.docx`, `test*.xls`, `test*.xlsx`, `test*.pdf`. |
| `--mark-suffix TEXT` | Marker file suffix. Default: `_LtH4Dk`. |
| `--quarantine-dir PATH` | Move removal matches into `PATH` instead of deleting them. |
| `--apply` | Perform file changes. Without this flag the tool stays in dry-run mode. |
| `--dry-run` | Force preview-only mode. Useful when overriding scripts. |
| `--once` | Run exactly one pass. Equivalent to `--passes 1`. |
| `--passes N` | Number of scan passes. `0` means run forever. Default: `1`. |
| `--interval SECONDS` | Delay between repeated passes. Default: `60`. |
| `--syslog` | Mirror log output to syslog. |
| `--verbose` | Print extra skip details. |
| `-h`, `--help` | Show built-in help. |

## Examples

Mark all files matching a custom pattern:

```sh
./service-mvp \
  --mark-dir ./incoming \
  --mark-pattern '*.pdf' \
  --mark-suffix '.marker' \
  --apply \
  --once
```

Quarantine office documents from two directories:

```sh
./service-mvp \
  --remove-dir "$HOME/Documents" \
  --remove-dir "$HOME/Desktop" \
  --quarantine-dir "$HOME/service-mvp-quarantine" \
  --apply \
  --once
```

Preview direct deletion candidates:

```sh
./service-mvp \
  --remove-dir ./old-files \
  --remove-pattern '*.tmp' \
  --once
```

## GitHub CI/CD

This repository includes GitHub Actions workflows:

- `.github/workflows/ci.yml`: runs on pushes, pull requests, and manual
  dispatches. It runs `shellcheck`, then builds and tests the project with both
  `gcc` and `clang`.
- `.github/workflows/release.yml`: runs on version tags like `v1.0.0`. It builds
  the binary, packages it as `service-mvp-linux-x86_64.tar.gz`, uploads it as a
  workflow artifact, and publishes it to the GitHub Release for the tag.

To publish a release:

```sh
git tag v1.0.0
git push origin v1.0.0
```

## Development

Recommended local loop:

```sh
make clean
make check
```

For a Clang build:

```sh
make clean
make check CC=clang
```
