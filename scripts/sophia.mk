.PHONY: sophia check-sophia clean-sophia
sophia: bemenu-renderer-sophia.so bemenu-sophia

SOPHIA_SDK = vendor/sophia-desktop-sdk/source
SOPHIA_SDK_LIBS = .artifacts/desktop-sdk/libsophia-desktop.a .artifacts/desktop-sdk/libsophia-9p.a
SOPHIA_SDK_INPUTS = $(wildcard $(SOPHIA_SDK)/src/*.h $(SOPHIA_SDK)/src/*.c $(SOPHIA_SDK)/src/nine_p/*.[ch] $(SOPHIA_SDK)/src/shell_files/*.[ch] $(SOPHIA_SDK)/src/shell_session/*.[ch] $(SOPHIA_SDK)/src/native_session/*.[ch])
$(SOPHIA_SDK_LIBS) &: $(SOPHIA_SDK_INPUTS) $(SOPHIA_SDK)/GNUmakefile
	$(MAKE) -C $(SOPHIA_SDK) BUILD=$(CURDIR)/.artifacts/desktop-sdk CFLAGS='-O2 -g -fPIC' $(addprefix $(CURDIR)/,$(SOPHIA_SDK_LIBS))

SOPHIA_WIRE = $(wildcard vendor/sophia-desktop-sdk/source/src/shell_wire/*.c)
SOPHIA_FILE_ADAPTER = $(wildcard lib/renderers/sophia/files*.c)
SOPHIA_SOURCES = lib/renderers/sophia/connection.c lib/renderers/sophia/connection_receive.c lib/renderers/sophia/connection_allocation.c lib/renderers/sophia/connection_schedule.c lib/renderers/sophia/connection_frames.c lib/renderers/sophia/connection_deadlines.c lib/renderers/sophia/view.c lib/renderers/sophia/catalog.c lib/renderers/sophia/input.c lib/renderers/sophia/raster.c $(SOPHIA_WIRE) $(SOPHIA_SDK_LIBS)
SOPHIA_HEADERS = lib/renderers/sophia/connection.h lib/renderers/sophia/connection_internal.h lib/renderers/sophia/view.h lib/renderers/sophia/catalog.h lib/renderers/sophia/input.h lib/renderers/sophia/raster.h lib/renderers/cairo_renderer.h $(wildcard vendor/sophia-desktop-sdk/source/src/*.h vendor/sophia-desktop-sdk/source/src/shell_wire/*.h)
SOPHIA_LIBS = $(shell $(PKG_CONFIG) --libs cairo pangocairo fontconfig) -lm
SOPHIA_INCLUDES = $(shell $(PKG_CONFIG) --cflags cairo pangocairo fontconfig)

bemenu-renderer-sophia.so: private override CPPFLAGS += $(SOPHIA_INCLUDES)
bemenu-renderer-sophia.so: lib/renderers/sophia/sophia.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) scripts/sophia.mk VERSION .git/index util.a | libbemenu.so
	$(LINK.c) -shared -fPIC -Wl,-z,defs $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

.artifacts/sophia-raster-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-raster-test: tests/sophia/raster.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) util.a | libbemenu.so
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

.artifacts/sophia-catalog-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_catalog_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-native-wire-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_native_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-native-codec-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_native_codec_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-resource-codec-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_resource_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-limits-codec-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_limits_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-feedback-codec-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_feedback_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-outbox-test: private override LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=send
.artifacts/sophia-outbox-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_outbox_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-upload-test: private override LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free
.artifacts/sophia-upload-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_upload_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-native-lifecycle-test: private override LDFLAGS += -Wl,--wrap=sophia_shell_outbox_commit
.artifacts/sophia-native-lifecycle-test: vendor/sophia-desktop-sdk/source/src/tests/sophia_shell_wire_native_lifecycle_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-connection-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-connection-test: private override LDFLAGS += -Wl,--wrap=sophia_shell_native_lifecycle_new -Wl,--wrap=sophia_shell_outbox_commit
.artifacts/sophia-connection-test: tests/sophia/connection.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) util.a | libbemenu.so
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

.artifacts/sophia-catalog-menu-test: private override LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc
.artifacts/sophia-catalog-menu-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-catalog-menu-test: tests/sophia/catalog.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) util.a | libbemenu.so
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

.artifacts/sophia-fonts-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-fonts-test: tests/sophia/fonts.c lib/renderers/sophia/fonts.c lib/renderers/sophia/fonts.h
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) $(SOPHIA_LIBS) -o $@

.artifacts/sophia-files-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-files-test: tests/sophia/files.c tests/sophia/files_peer.c $(SOPHIA_FILE_ADAPTER) $(wildcard lib/renderers/sophia/files*.h) $(filter-out .artifacts/desktop-sdk/libsophia-9p.a,$(SOPHIA_SOURCES)) $(SOPHIA_HEADERS) util.a | libbemenu.so
	mkdir -p .artifacts
	$(LINK.c) -UNDEBUG $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

check-sophia: .artifacts/sophia-files-test

check-sophia: .artifacts/sophia-fonts-test

check-sophia: sophia .artifacts/sophia-connection-test .artifacts/sophia-native-lifecycle-test .artifacts/sophia-outbox-test .artifacts/sophia-upload-test .artifacts/sophia-limits-codec-test .artifacts/sophia-feedback-codec-test .artifacts/sophia-resource-codec-test .artifacts/sophia-native-codec-test .artifacts/sophia-catalog-menu-test .artifacts/sophia-raster-test .artifacts/sophia-catalog-test .artifacts/sophia-native-wire-test
	env LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-connection-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-content.frames
	env LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-catalog-menu-test
	env LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-files-test
	python3 scripts/check-sophia-vendor.py
	python3 -B tests/sophia/vendor.py
	.artifacts/sophia-native-lifecycle-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-content.frames
	.artifacts/sophia-outbox-test
	.artifacts/sophia-upload-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-content.frames
	.artifacts/sophia-catalog-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-launcher.frames
	.artifacts/sophia-native-wire-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-native-launcher.frames
	.artifacts/sophia-native-codec-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-native-launcher.frames
	.artifacts/sophia-resource-codec-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-content.frames
	.artifacts/sophia-limits-codec-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-content.frames
	.artifacts/sophia-feedback-codec-test vendor/sophia-desktop-sdk/source/spec/golden/sophia-shell-content.frames
	python3 scripts/check-sophia-layout.py
	python3 tests/sophia/executable.py
	.artifacts/sophia-fonts-test
	env -u DISPLAY -u WAYLAND_DISPLAY -u WAYLAND_SOCKET BEMENU_BACKEND=sophia BEMENU_RENDERER=$(CURDIR)/bemenu-renderer-sophia.so LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-raster-test

clean: clean-sophia
clean-sophia:
	$(MAKE) -C $(SOPHIA_SDK) BUILD=$(CURDIR)/.artifacts/desktop-sdk clean
	rm -f .artifacts/sophia-files-test
	rm -f .artifacts/sophia-fonts-test
	rm -f bemenu-sophia bemenu-renderer-sophia.so .artifacts/sophia-connection-test .artifacts/sophia-native-lifecycle-test .artifacts/sophia-outbox-test .artifacts/sophia-upload-test .artifacts/sophia-limits-codec-test .artifacts/sophia-feedback-codec-test .artifacts/sophia-resource-codec-test .artifacts/sophia-native-codec-test .artifacts/sophia-catalog-menu-test .artifacts/sophia-raster-test .artifacts/sophia-catalog-test .artifacts/sophia-native-wire-test

# Dedicated persistent native client; does not use stdin or the one-shot runner.
# Link the same menu core into the selected executable: the protected launcher
# exposes that file, not its build directory or a sibling private shared library.
bemenu-sophia: private override CPPFLAGS += $(SOPHIA_INCLUDES)
bemenu-sophia: lib/renderers/sophia/main.c lib/renderers/sophia/fonts.c lib/renderers/sophia/fonts.h $(SOPHIA_FILE_ADAPTER) $(wildcard lib/renderers/sophia/files*.h) $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) lib/bemenu.h lib/internal.h $(BEMENU_CORE) scripts/sophia.mk
	$(LINK.c) $(filter %.c %.a,$^) $(SOPHIA_LIBS) -ldl -o $@

check-sophia: bemenu-sophia
