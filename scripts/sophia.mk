.PHONY: sophia check-sophia clean-sophia
sophia: bemenu-renderer-sophia.so

SOPHIA_WIRE = $(wildcard vendor/sophia-shell/shell_wire/*.c)
SOPHIA_SOURCES = lib/renderers/sophia/catalog.c lib/renderers/sophia/input.c lib/renderers/sophia/raster.c $(SOPHIA_WIRE)
SOPHIA_HEADERS = lib/renderers/sophia/catalog.h lib/renderers/sophia/input.h lib/renderers/sophia/raster.h lib/renderers/cairo_renderer.h $(wildcard vendor/sophia-shell/*.h vendor/sophia-shell/shell_wire/*.h)
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

.artifacts/sophia-catalog-menu-test: private override LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc
.artifacts/sophia-catalog-menu-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-catalog-menu-test: tests/sophia/catalog.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) util.a | libbemenu.so
	mkdir -p .artifacts
	$(LINK.c) $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

check-sophia: sophia .artifacts/sophia-native-codec-test .artifacts/sophia-catalog-menu-test .artifacts/sophia-raster-test .artifacts/sophia-catalog-test .artifacts/sophia-native-wire-test
	env LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-catalog-menu-test
	python3 scripts/check-sophia-vendor.py
	.artifacts/sophia-catalog-test vendor/sophia-shell/sophia-shell-launcher.frames
	.artifacts/sophia-native-wire-test vendor/sophia-shell/sophia-shell-native-launcher.frames
	.artifacts/sophia-native-codec-test vendor/sophia-shell/sophia-shell-native-launcher.frames
	python3 scripts/check-sophia-layout.py
	env -u DISPLAY -u WAYLAND_DISPLAY -u WAYLAND_SOCKET BEMENU_BACKEND=sophia BEMENU_RENDERER=$(CURDIR)/bemenu-renderer-sophia.so LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-raster-test

clean: clean-sophia
clean-sophia:
	rm -f bemenu-renderer-sophia.so .artifacts/sophia-native-codec-test .artifacts/sophia-catalog-menu-test .artifacts/sophia-raster-test .artifacts/sophia-catalog-test .artifacts/sophia-native-wire-test
