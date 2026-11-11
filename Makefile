# Native Android shell. penship.Pen extends NativeActivity and loads libpenship.so.

ANDROID_HOME ?= $(HOME)/.android-sdk
NDK := $(lastword $(sort $(wildcard $(ANDROID_HOME)/ndk/*)))
BUILD_TOOLS := $(lastword $(sort $(wildcard $(ANDROID_HOME)/build-tools/*)))
PLATFORM := $(lastword $(sort $(wildcard $(ANDROID_HOME)/platforms/android-*/android.jar)))
TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64
GLUE := $(NDK)/sources/android/native_app_glue
MIN_SDK := 24

AAPT2 := $(BUILD_TOOLS)/aapt2
ZIPALIGN := $(BUILD_TOOLS)/zipalign
APKSIGNER := $(BUILD_TOOLS)/apksigner
ADB := $(ANDROID_HOME)/platform-tools/adb

ABIS := arm64-v8a x86_64
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -fPIC -I$(GLUE) -Isrc -Ithird_party
GLUE_CFLAGS := -std=c11 -O2 -fPIC -I$(GLUE) -Wno-unused-parameter
LDFLAGS := -shared -pthread -Wl,-z,max-page-size=16384 -Wl,-u,ANativeActivity_onCreate \
	-landroid -llog -lEGL -lGLESv2 -lm

.PHONY: all test apk install clean
.SECONDARY:

all: test apk

build/doc_test: src/doc.c src/doc_test.c src/doc.h src/utf8.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Isrc -o $@ src/doc_test.c src/doc.c

build/page_test: src/page.c src/page_test.c src/page.h src/utf8.h third_party/stb_truetype.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Isrc -Ithird_party -o $@ src/page_test.c src/page.c -lm

build/chrome_test: src/chrome.c src/chrome_test.c src/chrome.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Isrc -o $@ src/chrome_test.c src/chrome.c

build/ime_test: src/ime.c src/ime_test.c src/ime.h src/utf8.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Isrc -pthread -o $@ src/ime_test.c src/ime.c

build/html_test: src/html.c src/html_test.c src/html.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Isrc -o $@ src/html_test.c src/html.c

build/fontlist_test: src/fontlist.c src/fontlist_test.c src/fontlist.h
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Isrc -o $@ src/fontlist_test.c src/fontlist.c

test: build/doc_test build/page_test build/chrome_test build/ime_test build/html_test build/fontlist_test
	./build/doc_test
	./build/page_test
	./build/chrome_test
	./build/ime_test
	./build/html_test
	./build/fontlist_test

build/obj/%/chrome.o: src/chrome.c src/chrome.h
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(CFLAGS) -c -o $@ src/chrome.c

build/obj/%/ime.o: src/ime.c src/ime.h src/utf8.h
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(CFLAGS) -c -o $@ src/ime.c

build/obj/%/main.o: src/main.c src/page.h src/doc.h src/chrome.h src/ime.h src/html.h src/utf8.h src/fontlist.h
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(CFLAGS) -c -o $@ src/main.c

build/obj/%/doc.o: src/doc.c src/doc.h src/utf8.h
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(CFLAGS) -c -o $@ src/doc.c

build/obj/%/page.o: src/page.c src/page.h src/utf8.h third_party/stb_truetype.h
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(CFLAGS) -c -o $@ src/page.c

build/obj/%/glue.o: $(GLUE)/android_native_app_glue.c
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(GLUE_CFLAGS) -c -o $@ $(GLUE)/android_native_app_glue.c

build/obj/%/html.o: src/html.c src/html.h
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(CFLAGS) -c -o $@ src/html.c

build/obj/%/fontlist.o: src/fontlist.c src/fontlist.h
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(CFLAGS) -c -o $@ src/fontlist.c

build/lib/%/libpenship.so: build/obj/%/main.o build/obj/%/doc.o build/obj/%/page.o build/obj/%/chrome.o build/obj/%/ime.o build/obj/%/html.o build/obj/%/fontlist.o build/obj/%/glue.o
	@test -n "$(NDK)" || (echo "NDK not found under $(ANDROID_HOME)/ndk" && exit 1)
	mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/$(call triple,$*)-clang $(LDFLAGS) -o $@ $^

# $(call triple,arm64-v8a) -> aarch64-linux-android24
triple = $(if $(filter arm64-v8a,$1),aarch64-linux-android$(MIN_SDK),$(if $(filter x86_64,$1),x86_64-linux-android$(MIN_SDK),$(error unknown abi $1)))

build/dex/classes.dex: android/Keyboard.java android/Pen.java
	mkdir -p build/classes build/dex
	javac --release 8 -cp $(PLATFORM) -d build/classes android/Keyboard.java android/Pen.java
	$(BUILD_TOOLS)/d8 --min-api $(MIN_SDK) --output build/dex build/classes/penship/*.class

build/compiled-res.zip: android/res/mipmap-mdpi/ic_launcher.png android/res/mipmap-hdpi/ic_launcher.png \
		android/res/mipmap-xhdpi/ic_launcher.png android/res/mipmap-xxhdpi/ic_launcher.png \
		android/res/mipmap-xxxhdpi/ic_launcher.png
	mkdir -p build
	$(AAPT2) compile --dir android/res -o $@

apk: test $(foreach abi,$(ABIS),build/lib/$(abi)/libpenship.so) build/dex/classes.dex android/AndroidManifest.xml build/compiled-res.zip
	@test -n "$(BUILD_TOOLS)" || (echo "build-tools not found under $(ANDROID_HOME)" && exit 1)
	mkdir -p build
	$(AAPT2) link -o build/linked.apk --manifest android/AndroidManifest.xml -I $(PLATFORM) \
		-R build/compiled-res.zip \
		--min-sdk-version $(MIN_SDK) --target-sdk-version 36
	python3 tools/pack_apk.py build/linked.apk build/unaligned.apk \
		$(foreach abi,$(ABIS),lib/$(abi)/libpenship.so=build/lib/$(abi)/libpenship.so) \
		assets/OFL.txt=assets/OFL.txt \
		assets/showcase.md=samples/showcase.md \
		classes.dex=build/dex/classes.dex
	$(ZIPALIGN) -P 16 -f 4 build/unaligned.apk build/aligned.apk
	@test -f build/debug.keystore || keytool -genkeypair -keystore build/debug.keystore \
		-storepass android -alias androiddebugkey -keypass android \
		-keyalg RSA -keysize 2048 -validity 10000 \
		-dname "CN=penship debug, OU=penship, O=penship, L=local, S=local, C=US"
	$(APKSIGNER) sign --ks build/debug.keystore --ks-pass pass:android --key-pass pass:android \
		--out build/penship.apk build/aligned.apk
	$(APKSIGNER) verify --verbose build/penship.apk

install: apk
	$(ADB) install -r build/penship.apk
	$(ADB) shell am start -n app.penship/penship.Pen

clean:
	rm -rf build
