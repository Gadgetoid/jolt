# Packages

JoLT installs extra programs and data with `jpm`. The format follows Arch Linux packages: a gzipped tarball with a `.PKGINFO` file at the root. Recipes live in [jolt-packages](https://github.com/Gadgetoid/jolt-packages).

## Package files

`NAME-VERSION-REL.pkg.tar.gz` holds the files at their installed paths, relative to `/`, and `.PKGINFO`:

```
pkgname = sacc
pkgver = 1.07-1
pkgdesc = Gopher client
url = gopher://bitreich.org/1/scm/sacc
license = ISC
arch = jolt2
size = 61440
```

| Key | |
| --- | --- |
| `pkgname` | Lower case letters, digits, `-`, `_`, `+` and `.` |
| `pkgver` | Upstream version, `-`, then the package release |
| `pkgdesc` | One line |
| `url`, `license` | Upstream home and licence |
| `arch` | `jolt` and the ABI version for programs (`jolt2`), `any` for data such as fonts and themes |
| `size` | Installed size in bytes |
| `depend` | Another package this one needs, once per package, names only |

Paths must be relative and must not contain `..`. Programs are static, so `depend` is for data and helper programs, not libraries. There are no install scripts.

## Repositories

A repository is a directory, served over HTTP(S) or on a card, with a subdirectory per ABI version holding the packages and an `index`. `jpm` reads `/proc/jolt` for the device's ABI and fetches `REPO/abi2/index`.

`index` is each package's `.PKGINFO`, plus `filename`, `csize` (the package file's size) and `sha256`, with a blank line between packages:

```
pkgname = sacc
pkgver = 1.07-1
...
filename = sacc-1.07-1.pkg.tar.gz
csize = 24576
sha256 = 3b0c...

pkgname = lrzsz
...
```

The repository is set in `/etc/jpm.conf`, or `JPM_REPO` for one command. The default is `https://github.com/Gadgetoid/jolt-packages/releases/download`, where each ABI version is a release: `abi2/index` is the `index` asset of the `abi2` release.

## On the device

`jpm` keeps the index in `/var/lib/jpm/index` and each installed package's `.PKGINFO` and file list in `/var/lib/jpm/NAME/`. It checks the sha256 and `arch` of every package, refuses paths outside the package and files another package owns, and installs dependencies first.

## Building

Each JoLT release has a `jolt-sdk-VERSION.tar.gz` with the C library, ncurses, LibreSSL, Mbed TLS, the linker script and a compiler wrapper. Programs must be built with it for the matching ABI. Its README explains the setup.
