# WineForge-Internal: native-steam/focused-client-hook-build-v1.
CC := /usr/bin/clang
CMAKE ?= cmake
OUT := build
MIN_VER := 15.0
DOBBY := deps/Dobby
CFLAGS := -arch arm64 -mmacosx-version-min=$(MIN_VER) -std=c17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -fPIC -MMD -MP -Isrc/dylib -Isrc/vendor -I$(OUT)/generated
SRCS := core/loader.c core/macho.c resolver/anchor.c resolver/aob.c resolver/resolver.c resolver/sigdb.c util/log.c util/file.c hooks/hooks.c hooks/hook_compat.c hooks/hook_webui.c hooks/hook_webpatch.c hooks/hook_spawn.c hooks/hook_launch.c feats/compat.c feats/webui.c feats/compatsvc.c feats/webpatch.c feats/wineforge.c feats/settings.c
# Unused upstream SteamUI extension groups are excluded from installation;
# definitions remain linked for the unchanged shared hooks infrastructure.
SRCS += hooks/hook_shortcut.c hooks/hook_icon.c util/peicon.c
OBJS := $(addprefix $(OUT)/,$(SRCS:.c=.o)) $(OUT)/cJSON.o
DOBBY_LIBS := $(OUT)/dobby/libdobby.a $(OUT)/dobby/builtin-plugin/SymbolResolver/libdobby_symbol_resolver.a $(OUT)/dobby/builtin-plugin/SymbolResolver/libmacho_ctx_kit.a $(OUT)/dobby/builtin-plugin/SymbolResolver/libshared_cache_ctx_kit.a $(OUT)/dobby/external/osbase/libosbase.a $(OUT)/dobby/external/logging/liblogging.a
.PHONY: all deps check-deps dobby overlay payload client-check client-check-tests settings-tests overlay-tests
all: $(OUT)/wineforge-native-steam.dylib $(OUT)/wineforge-overlay-shim.dylib $(OUT)/wineforge-client-check
overlay: $(OUT)/wineforge-overlay-shim.dylib
client-check: $(OUT)/wineforge-client-check
CLIENT_CHECK_SRCS := probe_anchors.c src/dylib/core/macho.c src/dylib/resolver/anchor.c src/dylib/resolver/aob.c src/dylib/resolver/resolver.c src/dylib/resolver/sigdb.c src/dylib/util/log.c src/dylib/util/file.c src/dylib/feats/compat.c src/vendor/cJSON.c
$(OUT)/wineforge-client-check: $(CLIENT_CHECK_SRCS) $(OUT)/generated/compat_run.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,-dead_strip $(CLIENT_CHECK_SRCS) -o $@
	codesign -fs - $@
client-check-tests: $(OUT)/test-client-check
	$(OUT)/test-client-check
$(OUT)/test-client-check: tests/client_check.c $(CLIENT_CHECK_SRCS) $(OUT)/generated/compat_run.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,-dead_strip tests/client_check.c $(filter-out probe_anchors.c,$(CLIENT_CHECK_SRCS)) -o $@
settings-tests: $(OUT)/test-settings-webpatch $(OUT)/settings-component-v1.js
	$(OUT)/test-settings-webpatch
	WINEFORGE_SETTINGS_COMPONENT="$(abspath $(OUT)/settings-component-v1.js)" node --test tests/settings_panel.test.mjs
SETTINGS_TEST_SRCS := tests/settings_webpatch.c src/dylib/util/file.c
$(OUT)/test-settings-webpatch: $(SETTINGS_TEST_SRCS) src/dylib/feats/webpatch.c src/dylib/hooks/hook_webpatch.c $(wildcard src/dylib/feats/*.h src/dylib/util/*.h) $(OUT)/generated/settings_panel.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,-dead_strip $(SETTINGS_TEST_SRCS) -o $@
$(OUT)/settings-component-v1.js: $(OUT)/test-settings-webpatch
	$< --emit-component > $@.tmp
	mv $@.tmp $@
deps:
	./scripts/fetch-deps.sh
check-deps:
	@test -d "$(DOBBY)/.git" || { echo 'Run make deps first.' >&2; exit 1; }
	@test "$$(git -C "$(DOBBY)" rev-parse HEAD)" = 5dfc8546954ce3b3198132ab13fddb89ee92cdd7
	@test -z "$$(git -C "$(DOBBY)" status --porcelain)"
dobby: check-deps
	$(CMAKE) -S $(DOBBY) -B $(OUT)/dobby -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$(MIN_VER) -DDOBBY_DEBUG=OFF -DDOBBY_GENERATE_SHARED=OFF -G 'Unix Makefiles'
	$(CMAKE) --build $(OUT)/dobby --parallel 2
$(OUT)/generated/compat_run.h: compat_run.sh src/dylib/embed_script.py
	@mkdir -p $(dir $@)
	python3 src/dylib/embed_script.py $< $@ RUN_SCRIPT
$(OUT)/feats/compat.o: $(OUT)/generated/compat_run.h
$(OUT)/generated/settings_panel.h: settings_panel.js src/dylib/embed_script.py
	@mkdir -p $(dir $@)
	python3 src/dylib/embed_script.py $< $@ WF_SETTINGS_PANEL_JS
$(OUT)/feats/webpatch.o: $(OUT)/generated/settings_panel.h
$(OUT)/%.o: src/dylib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/cJSON.o: src/vendor/cJSON.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
# Each archive is produced by the pinned CMake build above.
$(DOBBY_LIBS): | dobby
	@test -f "$@"
$(OUT)/wineforge-native-steam.arm64.dylib: $(OBJS) $(DOBBY_LIBS)
	$(CC) -arch arm64 -mmacosx-version-min=$(MIN_VER) -dynamiclib -install_name @rpath/wineforge-native-steam.dylib -o $@ $(OBJS) $(DOBBY_LIBS) -framework CoreFoundation -framework CoreGraphics -lc++
$(OUT)/wineforge-native-steam.x86_64.dylib: src/dylib/stub_x86_64.c
	@mkdir -p $(dir $@)
	$(CC) -arch x86_64 -mmacosx-version-min=$(MIN_VER) -dynamiclib -install_name @rpath/wineforge-native-steam.dylib -o $@ $<
$(OUT)/wineforge-native-steam.dylib: $(OUT)/wineforge-native-steam.arm64.dylib $(OUT)/wineforge-native-steam.x86_64.dylib
	lipo -create $^ -output $@
	codesign -fs - $@
# WineForge-Internal: steam-play/focused-overlay-shim-build-v1.
$(OUT)/wineforge-overlay-shim.%.dylib: src/overlay/overlay_shim.m
	@mkdir -p $(dir $@)
	$(CC) -arch $* -mmacosx-version-min=$(MIN_VER) -dynamiclib -O2 -fblocks -fno-objc-arc -Wall -Wextra -Wno-deprecated-declarations -install_name @rpath/wineforge-overlay-shim.dylib -o $@ $< -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -framework CoreFoundation
$(OUT)/wineforge-overlay-shim.dylib: $(OUT)/wineforge-overlay-shim.arm64.dylib $(OUT)/wineforge-overlay-shim.x86_64.dylib
	lipo -create $^ -output $@
	codesign -fs - $@
-include $(OBJS:.o=.d)

# WineForge-Internal: native-steam/standalone-candidate-payload-v1.
payload: all
	python3 scripts/package-payload.py

# Upstream ownership regression uses mock Metal objects, no Steam/game/GPU.
overlay-tests: $(OUT)/overlay-ownership.arm64 $(OUT)/overlay-ownership.x86_64
	$(OUT)/overlay-ownership.arm64
	$(OUT)/overlay-ownership.x86_64
$(OUT)/overlay-ownership.%: src/overlay/tests/ownership.m src/overlay/tests/check.h src/overlay/overlay_shim.m
	@mkdir -p $(dir $@)
	$(CC) -arch $* -mmacosx-version-min=$(MIN_VER) -O2 -fblocks -fno-objc-arc -Wall -Wextra -Wno-deprecated-declarations -o $@ $< -framework Foundation -framework Metal -framework QuartzCore -framework AppKit
