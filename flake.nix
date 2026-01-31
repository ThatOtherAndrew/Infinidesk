{
  description = "Infinidesk - Infinite Canvas Wayland Compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages = {
          infinidesk = pkgs.stdenv.mkDerivation {
            pname = "infinidesk";
            version = "0.1.0";

            src = ./.;

            nativeBuildInputs = with pkgs; [
              meson
              ninja
              pkg-config
              wayland-scanner
            ];

            buildInputs = with pkgs; [
              wlroots_0_18
              wayland
              wayland-protocols
              libxkbcommon
              pixman
              xorg.libxcb
            ];

            meta = with pkgs.lib; {
              description = "Infinite canvas Wayland compositor";
              homepage = "https://github.com/your-username/infinidesk";
              license = licenses.mit;
              platforms = platforms.linux;
              mainProgram = "infinidesk";
            };
          };

          default = self.packages.${system}.infinidesk;
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.infinidesk ];

          packages = with pkgs; [
            # Development tools
            gdb
            valgrind
            clang-tools  # clangd, clang-format
            bear         # Generate compile_commands.json

            # Testing
            kitty
            foot
            weston
          ];

          shellHook = ''
            echo "Infinidesk development shell"
            echo ""
            echo "Build commands:"
            echo "  meson setup build        # Configure"
            echo "  ninja -C build           # Build"
            echo "  ./build/infinidesk -d    # Run with debug logging"
            echo ""
            echo "Generate compile_commands.json for IDE support:"
            echo "  bear -- ninja -C build"
            echo ""
          '';
        };

        # For running directly with: nix run
        apps.default = {
          type = "app";
          program = "${self.packages.${system}.infinidesk}/bin/infinidesk";
        };
      }
    );
}
