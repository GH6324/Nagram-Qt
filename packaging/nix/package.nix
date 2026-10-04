# Nagram Desktop built from source: the telegram-desktop package of nixpkgs
# with the Nagram source. A build from source cannot hide what it compiles in,
# so the caller passes Telegram API credentials of their own, see
# https://core.telegram.org/api/obtaining_api_id. package-bin.nix installs the
# released binary, which carries the credentials of the project.
{
  lib,
  stdenv,
  telegram-desktop,
  pango,
  tlottie,
  src,
  version,
  apiId,
  apiHash,
}:

telegram-desktop.override {
  pname = "nagram-desktop";
  unwrapped = telegram-desktop.unwrapped.overrideAttrs (
    finalAttrs: previousAttrs: {
      pname = "nagram-desktop-unwrapped";
      inherit version src;

      # nixpkgs adds these two only to the package named telegram-desktop-unwrapped.
      buildInputs =
        previousAttrs.buildInputs ++ [ tlottie ] ++ lib.optionals stdenv.hostPlatform.isLinux [ pango ];

      cmakeFlags = lib.filter (flag: !lib.hasInfix "TDESKTOP_API_" flag) previousAttrs.cmakeFlags ++ [
        (lib.cmakeFeature "TDESKTOP_API_ID" (toString apiId))
        (lib.cmakeFeature "TDESKTOP_API_HASH" apiHash)
      ];

      passthru = removeAttrs previousAttrs.passthru [ "updateScript" ];

      meta = previousAttrs.meta // {
        mainProgram = "Nagram";
        description = "Independent Telegram client based on Telegram Desktop";
        longDescription = ''
          Nagram Desktop is Telegram Desktop with additional interface,
          privacy and message options. Its automatic updater is not built:
          the package is updated through Nix.
        '';
        homepage = "https://github.com/NextAlone/Nagram-qt";
        changelog = "https://github.com/NextAlone/Nagram-qt/releases/tag/v${finalAttrs.version}";
        maintainers = [ ];
      };
    }
  );
}
