{ pkgs ? import <nixpkgs> {} }: pkgs.callPackage (

{ mkShell

, meson
, ninja
, pkg-config

, libxcb
, xcbutilkeysyms
, xcbutilimage
, xcbutilxrm
, pam
, libX11
, libev
, cairo
, libxkbcommon
, libxkbfile
}:

mkShell {
  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];
  buildInputs = [
    libxcb
    xcbutilkeysyms
    xcbutilimage
    xcbutilxrm
    pam
    libX11
    libev
    cairo
    libxkbcommon
    libxkbfile
  ];
}

) {}
