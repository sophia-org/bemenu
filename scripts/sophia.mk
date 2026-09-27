.PHONY: sophia check-sophia check-sophia-sdk clean-sophia
sophia: bemenu-renderer-sophia.so bemenu-sophia

SOPHIA_SDK = vendor/sophia-desktop-sdk/source
SOPHIA_SDK_LIBS = .artifacts/desktop-sdk/libsophia-desktop.a .artifacts/desktop-sdk/libsophia-9p.a
SOPHIA_SDK_INPUTS = $(wildcard $(SOPHIA_SDK)/src/*.h $(SOPHIA_SDK)/src/*.c $(SOPHIA_SDK)/src/nine_p/*.[ch] $(SOPHIA_SDK)/src/shell_files/*.[ch] $(SOPHIA_SDK)/src/shell_session/*.[ch] $(SOPHIA_SDK)/src/native_session/*.[ch])
$(SOPHIA_SDK_LIBS) &: $(SOPHIA_SDK_INPUTS) $(SOPHIA_SDK)/GNUmakefile
	$(MAKE) -C $(SOPHIA_SDK) WITH_IPC=0 BUILD=$(CURDIR)/.artifacts/desktop-sdk CFLAGS='-O2 -g -fPIC' $(addprefix $(CURDIR)/,$(SOPHIA_SDK_LIBS))

SOPHIA_FILE_ADAPTER = $(wildcard lib/renderers/sophia/files*.c)
SOPHIA_SOURCES = lib/renderers/sophia/view.c lib/renderers/sophia/catalog.c lib/renderers/sophia/input.c lib/renderers/sophia/raster.c $(SOPHIA_SDK_LIBS)
SOPHIA_HEADERS = lib/renderers/sophia/view.h lib/renderers/sophia/catalog.h lib/renderers/sophia/input.h lib/renderers/sophia/raster.h lib/renderers/cairo_renderer.h $(wildcard $(SOPHIA_SDK)/src/*.h)
SOPHIA_LIBS = $(shell $(PKG_CONFIG) --libs cairo pangocairo fontconfig) -lm
SOPHIA_INCLUDES = $(shell $(PKG_CONFIG) --cflags cairo pangocairo fontconfig)

bemenu-renderer-sophia.so: private override CPPFLAGS += $(SOPHIA_INCLUDES)
bemenu-renderer-sophia.so: lib/renderers/sophia/sophia.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) scripts/sophia.mk VERSION .git/index util.a | libbemenu.so
	$(LINK.c) -shared -fPIC -Wl,-z,defs $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

.artifacts/sophia-raster-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-raster-test: tests/sophia/raster.c $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) util.a | libbemenu.so
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

# The unit fixture supplies ss outcomes. Keep the real ns and file codec, and
# exclude the 9P archive so an accidental real session dependency fails to link.
.artifacts/sophia-files-test: private override CPPFLAGS += $(SOPHIA_INCLUDES)
.artifacts/sophia-files-test: tests/sophia/files.c tests/sophia/files_peer.c $(SOPHIA_FILE_ADAPTER) $(wildcard lib/renderers/sophia/files*.h) $(filter-out .artifacts/desktop-sdk/libsophia-9p.a,$(SOPHIA_SOURCES)) $(SOPHIA_HEADERS) util.a | libbemenu.so
	mkdir -p .artifacts
	$(LINK.c) -UNDEBUG $(filter %.c %.a,$^) $(SOPHIA_LIBS) -L. -lbemenu -o $@

check-sophia-sdk: $(SOPHIA_SDK_LIBS)
	$(MAKE) -C $(SOPHIA_SDK) WITH_IPC=0 BUILD=$(CURDIR)/.artifacts/desktop-sdk CFLAGS='-O2 -g -fPIC' check

check-sophia: sophia check-sophia-sdk .artifacts/sophia-files-test .artifacts/sophia-fonts-test .artifacts/sophia-catalog-menu-test .artifacts/sophia-raster-test
	env LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-catalog-menu-test
	env LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-files-test
	python3 scripts/check-sophia-vendor.py
	python3 -B tests/sophia/vendor.py
	python3 scripts/check-sophia-layout.py
	python3 -B tests/sophia/executable.py
	.artifacts/sophia-fonts-test
	env -u DISPLAY -u WAYLAND_DISPLAY -u WAYLAND_SOCKET BEMENU_BACKEND=sophia BEMENU_RENDERER=$(CURDIR)/bemenu-renderer-sophia.so LD_LIBRARY_PATH=$(CURDIR) .artifacts/sophia-raster-test

clean: clean-sophia
clean-sophia:
	$(MAKE) -C $(SOPHIA_SDK) WITH_IPC=0 BUILD=$(CURDIR)/.artifacts/desktop-sdk clean
	rm -f bemenu-sophia bemenu-renderer-sophia.so .artifacts/sophia-files-test .artifacts/sophia-fonts-test .artifacts/sophia-catalog-menu-test .artifacts/sophia-raster-test

# Dedicated persistent native client. The protected launcher exposes this
# executable, not its build directory or a sibling private shared library.
bemenu-sophia: private override CPPFLAGS += $(SOPHIA_INCLUDES)
bemenu-sophia: lib/renderers/sophia/main.c lib/renderers/sophia/fonts.c lib/renderers/sophia/fonts.h $(SOPHIA_FILE_ADAPTER) $(wildcard lib/renderers/sophia/files*.h) $(SOPHIA_SOURCES) $(SOPHIA_HEADERS) lib/bemenu.h lib/internal.h $(BEMENU_CORE) scripts/sophia.mk
	$(LINK.c) $(filter %.c %.a,$^) $(SOPHIA_LIBS) -ldl -o $@
