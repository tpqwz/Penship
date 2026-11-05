# penship

A native Android markdown editor. The name is penship for now. This is **0.1.3-alpha**.

![The sample, rendered, with the menu mark](docs/writing.png)

![Open, Save, and the other options](docs/menu.png)

The pictures are the app on an Android emulator. The long sample is the note a new install starts with. The menu mark opens Open, Save, and the other options. The word count is on that screen.

A push to `master` builds the app and publishes a pre-release. The attached file, `penship-android.apk`, is the Android package only: `arm64-v8a` and `x86_64` in one APK, signed with a debug keystore from that build. A tag such as `v0.2.0` publishes a release of the same Android package.

MarkText is the product to aim at: one writing surface, markdown rendered as you type, CommonMark and GitHub Flavored Markdown, source mode, typewriter mode, focus mode, themes, and a way to get the document back out. Scribe is the other reference, for a native editor that paints markdown on the text itself and stays a small program.

penship is C. The activity is `penship.Pen`, a subclass of `android.app.NativeActivity`, the same approach as [rawdrawandroid](https://github.com/cnlohr/rawdrawandroid): a manifest and a shared library. The NDK's `native_app_glue` is compiled in from the NDK. It is not vendored here. `android/Keyboard.java` asks for the soft keyboard on the main thread. `android/Pen.java` is the activity, and it also reads the file Android handed over.

The page is one UTF-8 document. `src/doc.c` keeps the bytes in a rope of chunks and the caret. `src/page.c` keeps a line index, reuses it while the note and the width stay the same, and paints the lines on screen with Liberation Sans, top row first, and keeps the caret on screen. The on-screen keyboard commits text into the note. A menu mark at the end of the page opens Open, Save, Smaller, Larger, Font, Typewriter, Focus, Theme, and Source. Done returns to the page. Open and Save both use the system file picker. Smaller and Larger change the note between 14 and 32 sp. The first size is 24 sp. Font cycles sans, serif, and mono. Those choices are saved and applied the next time the app opens. A new install starts from the long sample in the package. Ctrl+Shift+T keeps the caret line in the middle. Ctrl+Shift+F dims every other line. Ctrl+Shift+P switches the day and night palettes. A double tap opens that line, or the whole fence or table, in source. A single tap only moves the caret. Other lines are rendered: a heading without its hashes, a bullet instead of the list mark, a bar instead of `>`, and a fenced block on a wash without its ticks. A double tap inside a fence opens the whole fence. Shift and an arrow key selects, and typing or backspace replaces that selection. Ctrl+Z undoes, and 32 edits are kept. Both are checked on the host. On Android, key events go into the document and a tap or focus brings up the soft keyboard. The launcher opens the private note `note.md`. The share sheet can hand penship a text or markdown file, or a piece of shared text, and penship opens it. The Open with sheet can hand over a text file or a markdown file: `.md`, `.markdown`, `.mdown`, `.mkd`, `.mkdn`, `.mdwn`, `.mdtxt`, `.mdtext`, `.mdx`, `.qmd`, `.rmd`, `.myst`, `.txt`, `.text`, and any `text/*` type. Edits write back to that file when Android granted write access. A read-only open keeps the edits in the private file `opened.md` and leaves `note.md` alone. `samples/showcase.md` is the long note that demonstrates the marks.

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
