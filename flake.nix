{
  description = "Nagram Desktop, an independent Telegram client based on Telegram Desktop";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;
      # The value of a "Name value" line of the version files.
      field =
        file: name:
        builtins.head (
          lib.findFirst (found: found != null) (throw "no ${name} line in ${toString file}") (
            map (builtins.match "${name} +([^ ]+)") (lib.splitString "\n" (builtins.readFile file))
          )
        );
      # Only what the build reads, so that other changes do not rebuild it.
      src = lib.fileset.toSource {
        root = ./.;
        fileset = lib.fileset.unions [
          ./CMakeLists.txt
          ./Telegram
          ./cmake
          ./lib
          ./changelog.txt
          ./LEGAL
          ./LICENSE
        ];
      };
      # Written by tools/nagram/nix_release.py after every stable release.
      released = ./packaging/nix/release.json;
    in
    {
      # The released binary, which carries the API credentials of the project.
      packages = lib.optionalAttrs (builtins.pathExists released) {
        x86_64-linux = rec {
          nagram-desktop = nixpkgs.legacyPackages.x86_64-linux.callPackage ./packaging/nix/package-bin.nix {
            inherit (lib.importJSON released) version hash;
          };
          default = nagram-desktop;
        };
      };

      # A build from source, with Telegram API credentials of the caller:
      #   nagram.lib.fromSource { pkgs = ...; apiId = 12345; apiHash = "..."; }
      # It needs the Git submodules: refer to this flake with ?submodules=1.
      lib.fromSource =
        {
          pkgs,
          apiId,
          apiHash,
        }:
        pkgs.callPackage ./packaging/nix/package.nix {
          inherit src apiId apiHash;
          version = "${field ./Telegram/build/version "AppVersionStr"}.${field ./Telegram/build/nagram_version "NagramRevision"}";
        };
    };
}
