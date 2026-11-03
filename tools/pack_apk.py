#!/usr/bin/env python3
"""Copy an aapt2 apk and add uncompressed native libraries."""

import sys
import zipfile


def main() -> None:
    if len(sys.argv) < 4:
        raise SystemExit("usage: pack_apk.py in.apk out.apk name=path ...")
    src, dst, *pairs = sys.argv[1:]
    with zipfile.ZipFile(src, "r") as zin, zipfile.ZipFile(dst, "w") as zout:
        for item in zin.infolist():
            zout.writestr(item, zin.read(item.filename))
        for pair in pairs:
            name, path = pair.split("=", 1)
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = 0o644 << 16
            with open(path, "rb") as handle:
                zout.writestr(info, handle.read())


if __name__ == "__main__":
    main()
