# penship

A native Android markdown editor. The name is penship for now.

MarkText is the product to aim at: one writing surface, markdown rendered as you type, CommonMark and GitHub Flavored Markdown, source mode, typewriter mode, focus mode, themes, and a way to get the document back out. Scribe is the other reference, for a native editor that paints markdown on the text itself and stays a small program.

penship is C. The activity is `android.app.NativeActivity`, the same approach as [rawdrawandroid](https://github.com/cnlohr/rawdrawandroid): a manifest, a shared library, and no Java sources. The NDK's `native_app_glue` is compiled in from the NDK. It is not vendored here.

The APK this tree builds is the window. It opens a paper-colored page and draws the name. Parsing, the caret, the keyboard, and files are not in it yet. `src/page.c` is the buffer those will paint into: tightly packed RGBA, top row first, checked on the host without a device.

## Build

The Android SDK and NDK are required. The Makefile uses `$ANDROID_HOME`, or `~/.android-sdk` when that is unset.

```sh
make
```

`make test` runs the page rasterizer on the host. `make apk` writes `build/penship.apk` for `arm64-v8a` and `x86_64`. With a device or emulator attached:

```sh
make install
```

The package is `app.penship`. The launcher label is penship. The debug APK is signed with a keystore under `build/`, which is not part of the tree.
