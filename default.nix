{
  pkgs ? import <nixpkgs> {}
, stdenv ? pkgs.stdenv
, debug ? true
, aeron-cpp ? pkgs.aeron-cpp
}:

let
  name = "aeron-capnp";

  add-links = ''
    ln --symbolic --force --target-directory=src \
      "${pkgs.ekam.src}/src/ekam/rules"
  '';
in

stdenv.mkDerivation {

  inherit name;
  src = ./.;

  buildInputs = with pkgs; [
    aeron-cpp
    capnproto
    openssl
    zlib
  ];

  nativeBuildInputs = with pkgs; [
    clang-tools
    ekam
    gtest
    which
  ];

  propagatedBuildInputs = with pkgs; [
  ];

  CAPNPC_FLAGS = with pkgs; [
    "-I${capnproto}/include"
  ];

  shellHook = add-links;

  buildPhase = ''
    ${add-links}
    make ${if debug then "debug" else "release"}
  '';

  installPhase = ''
    install --verbose -D --mode=644 \
      --target-directory="''${!outputLib}/lib" \
      lib${name}.a

    install --verbose -D --mode=644 \
      --target-directory="''${!outputInclude}/include/${name}" \
      src/*.capnp \
      src/*.capnp.h \
      tmp/*.capnp.h \
      src/*.h 
  '';
}
