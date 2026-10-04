{
   description = "bemenu";

   inputs = {
      nixpkgs.url = "github:nixos/nixpkgs";
      flake-utils.url = "github:numtide/flake-utils";
      # The niltempus desktop's pinned nixpkgs, used only for the Sophia
      # renderer outputs below; the upstream bemenu package is unchanged.
      nixpkgs-sophia.url = "github:NixOS/nixpkgs/c59305bab2065cfecc4944690d9eedbb56f3a9fa";
   };

   outputs = { self, nixpkgs, flake-utils, nixpkgs-sophia }: let
      outputs = (flake-utils.lib.eachDefaultSystem (system: let
         pkgs = nixpkgs.outputs.legacyPackages.${system};
      in {
         packages.default = pkgs.callPackage ./default.nix {};
      }));

      # bemenu-sophia for the niltempus desktop, built with the release
      # builder's exact make command (niltempus installer/build.go) in Nix's
      # sandbox. The source revision stands in for the commit the builder
      # passes as GIT_SHA1 and GIT_TAG.
      system = "x86_64-linux";
      sp = nixpkgs-sophia.legacyPackages.${system};
      revision = self.rev or self.dirtyRev or "unknown";
      common = {
         src = self;
         nativeBuildInputs = [ sp.pkg-config ];
         buildInputs = [ sp.cairo sp.pango sp.fontconfig ];
         dontConfigure = true;
      };
      bemenu-sophia = sp.gcc14Stdenv.mkDerivation (common // {
         pname = "bemenu-sophia";
         version = "0.0.0-nix-measurement";
         buildPhase = ''
            runHook preBuild
            make -j$NIX_BUILD_CORES bemenu-sophia EXTRA_WARNINGS=-Werror \
               GIT_SHA1=${revision} GIT_TAG=${revision}
            runHook postBuild
         '';
         installPhase = ''
            runHook preInstall
            install -Dm755 bemenu-sophia $out/bin/bemenu-sophia
            runHook postInstall
         '';
      });
   in outputs // {
      packages = outputs.packages // {
         ${system} = outputs.packages.${system} // { inherit bemenu-sophia; };
      };
      checks.${system} = {
         inherit bemenu-sophia;
         # Bemenu's own Sophia renderer gate: the vendored SDK's checks, the
         # renderer tests and the Python layout and vendor checks.
         sophia = sp.gcc14Stdenv.mkDerivation (common // {
            name = "bemenu-sophia-check";
            nativeBuildInputs = common.nativeBuildInputs ++ [ sp.python3 sp.bubblewrap ];
            # The executable test starts bemenu-sophia, which loads fonts from
            # the host paths /usr/share/fonts and /usr/local/share/fonts before
            # it connects. Nix's build sandbox has no /usr, so the gate runs in
            # a private root where a pinned font set is /usr/share/fonts.
            # The raster test initializes fontconfig's default configuration.
            FONTCONFIG_FILE = sp.makeFontsConf { fontDirectories = [ sp.dejavu_fonts ]; };
            buildPhase = ''
               export HOME=$TMPDIR XDG_CACHE_HOME=$TMPDIR/cache
               patchShebangs scripts tests
               binds=()
               for entry in /*; do
                  [ "$entry" = /proc ] || [ "$entry" = /dev ] || binds+=(--bind "$entry" "$entry")
               done
               bwrap --tmpfs / "''${binds[@]}" --proc /proc --dev /dev \
                  --ro-bind ${sp.dejavu_fonts}/share/fonts /usr/share/fonts -- \
                  make -j$NIX_BUILD_CORES check-sophia EXTRA_WARNINGS=-Werror \
                  GIT_SHA1=${revision} GIT_TAG=${revision}
            '';
            installPhase = "touch $out";
         });
      };
   };
}
