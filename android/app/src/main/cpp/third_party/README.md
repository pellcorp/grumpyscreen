# Android third-party sources

## FreeType 2.13.3

`freetype-2.13.3.tar.xz` is the unmodified upstream source release. It is
vendored so Android builds can unpack and compile FreeType without depending
on an external download server.

* Upstream: https://freetype.org/
* Source: https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
* Alternative upstream distribution: https://sourceforge.net/projects/freetype/files/freetype2/2.13.3/
* SHA-256: `0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289`
* License: FreeType License (FTL) or GPLv2; see `LICENSE.TXT`,
  `docs/FTL.TXT`, and `docs/GPLv2.TXT` inside the archive.

When upgrading, replace the archive and update the filename and checksum in
`../CMakeLists.txt`, along with this documentation and `android/README.md`.

## NanoSVG

See `nanosvg/README.md` and `nanosvg/LICENSE.txt`.
