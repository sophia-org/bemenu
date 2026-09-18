.PHONY: sophia check-sophia clean-sophia
sophia: bemenu-renderer-sophia.so

SOPHIA_WIRE = $(wildcard vendor/sophia-shell/shell_wire/*.c)
SOPHIA_SOURCES = lib/renderers/sophia/connection.c lib/renderers/sophia/connection_receive.c lib/renderers/sophia/connection_allocation.c lib/renderers/sophia/connection_schedule.c lib/renderers/sophia/view.c lib/renderers/sophia/catalog.c lib/renderers/sophia/input.c lib/renderers/sophia/raster.c $(SOPHIA_WIRE)
SOPHIA_HEADERS = lib/renderers/sophia/connection.h lib/renderers/sophia/connection_internal.h lib/renderers/sophia/view.h lib/renderers/sophia/catalog.h lib/renderers/sophia/input.h lib/renderers/sophia/raster.h lib/renderers/cairo_renderer.h $(wildcard vendor/sophia-shell/*.h vendor/sophia-shell/shell_wire/*.h)
SOPHIA_LIBS = $(shell $(PKG_CONFIG) --libs cairo pangocairo fontconfig) -lm
SOPHIA_INCLUDES = $(shell $(PKG_CONFIG) --cflags cairo pangocairo fontconfig)

bemenu-renderer-sophia.so: private override CPPFLAGS += $(SOPHIA_INCLUDES)
bemenu-renderer-sophia.so: lib/renderers/sophia/sophia.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) scripts/sophia.mk VERSION .git/index util.a | libbemenu.so
	$(LINK.c) -shared -fPIC -Wl,-z,defs $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

.artifacts/sophia-raster-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-raster-test: tests/sophia/raster.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) util.a | libbemenu.so
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

.artifacts/sophia-catalog-test: vendor/sophia-shell/tests/sophia_shell_wire_catalog_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-native-wire-test: vendor/sophia-shell/tests/sophia_shell_wire_native_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-native-codec-test: vendor/sophia-shell/tests/sophia_shell_wire_native_codec_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-resource-codec-test: vendor/sophia-shell/tests/sophia_shell_wire_resource_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-limits-codec-test: vendor/sophia-shell/tests/sophia_shell_wire_limits_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-feedback-codec-test: vendor/sophia-shell/tests/sophia_shell_wire_feedback_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-outbox-test: private override LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=send
.artifacts/sophia-outbox-test: vendor/sophia-shell/tests/sophia_shell_wire_outbox_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-upload-test: private override LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free
.artifacts/sophia-upload-test: vendor/sophia-shell/tests/sophia_shell_wire_upload_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c,$^) -o $@

.artifacts/sophia-native-lifecycle-test: private override LDFLAGS += -Wl,--wrap=sophia_shell_outbox_commit
.artifacts/sophia-native-lifecycle-test: vendor/sophia-shell/tests/sophia_shell_wire_native_lifecycle_test.c $(SOPHIA_WIRE) $(SOPHIA_HEADERS)
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

check-sophia: sophia .artifacts/sophia-connection-test .artifacts/sophia-native-lifecycle-test .artifacts/sophia-outbox-test .artifacts/sophia-upload-test .artifacts/sophia-limits-codec-test .artifacts/sophia-feedback-codec-test .artifacts/sophia-resource-codec-test .artifacts/sophia-native-codec-test .artifacts/sophia-catalog-menu-test .artifacts/sophia-raster-test .artifacts/sophia-catalog-test .artifacts/sophia-native-wire-test
	env LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-connection-test vendor/sophia-shell/sophia-shell-content.frames
	env LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-catalog-menu-test
	python3 scripts/check-sophia-vendor.py
	.artifacts/sophia-native-lifecycle-test vendor/sophia-shell/sophia-shell-content.frames
	.artifacts/sophia-outbox-test
	.artifacts/sophia-upload-test vendor/sophia-shell/sophia-shell-content.frames
	.artifacts/sophia-catalog-test vendor/sophia-shell/sophia-shell-launcher.frames
	.artifacts/sophia-native-wire-test vendor/sophia-shell/sophia-shell-native-launcher.frames
	.artifacts/sophia-native-codec-test vendor/sophia-shell/sophia-shell-native-launcher.frames
	.artifacts/sophia-resource-codec-test vendor/sophia-shell/sophia-shell-content.frames
	.artifacts/sophia-limits-codec-test vendor/sophia-shell/sophia-shell-content.frames
	.artifacts/sophia-feedback-codec-test vendor/sophia-shell/sophia-shell-content.frames
	python3 scripts/check-sophia-layout.py
	env -u DISPLAY -u WAYLAND_DISPLAY -u WAYLAND_SOCKET BEMENU_BACKEND=sophia BEMENU_RENDERER=$(CURDIR)/bemenu-renderer-sophia.so LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-raster-test

clean: clean-sophia
clean-sophia:
	rm -f bemenu-renderer-sophia.so .artifacts/sophia-connection-test .artifacts/sophia-native-lifecycle-test .artifacts/sophia-outbox-test .artifacts/sophia-upload-test .artifacts/sophia-limits-codec-test .artifacts/sophia-feedback-codec-test .artifacts/sophia-resource-codec-test .artifacts/sophia-native-codec-test .artifacts/sophia-catalog-menu-test .artifacts/sophia-raster-test .artifacts/sophia-catalog-test .artifacts/sophia-native-wire-test
