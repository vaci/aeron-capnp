let
  capnproto_overlay = (self: super: {
    capnproto = super.capnproto.overrideAttrs (prev: {
      version = "git";
      src = self.fetchFromGitHub {
        owner = "capnproto";
        repo = "capnproto";
        rev = "c264e357d6f0304d04fb1267066d7d6ec7436bb3";
        sha256 = "sha256-p8E74KoxD9wZ1+2qpalfF/SQs1Axeq4pNWoX+BIW18I=";
      };
    });
   });
in {
  pkgs ? import <nixpkgs> {
    #config.replaceStdenv = { pkgs }: pkgs.clangStdenv;
    #overlays = [capnproto_overlay];
  }
, stdenv ? pkgs.stdenv
, debug ? true
}:

   
let
  name = "aeron-capnp";

  add-links = ''
    ln --symbolic --force --target-directory=src \
      "${pkgs.ekam.src}/src/ekam/rules"

    mkdir --parents src/compiler
    ln --symbolic --force "${pkgs.capnproto}/bin/capnp" src/compiler/capnp
    ln --symbolic --force "${pkgs.capnproto}/bin/capnpc-c++" src/compiler/capnpc-c++

    mkdir --parents src/capnp/compat
    ln --symbolic --force "${pkgs.capnproto.src}/c++/src/capnp/compat/byte-stream.capnp" src/capnp/compat/
    ln --symbolic --force "${pkgs.capnproto.src}/c++/src/capnp/compat/byte-stream.h" src/capnp/compat/
    ln --symbolic --force "${pkgs.capnproto.src}/c++/src/capnp/compat/byte-stream.c++" src/capnp/compat/
    ln --symbolic --force "${pkgs.capnproto.src}/c++/src/capnp/compat/http-over-capnp.capnp" src/capnp/compat/
    ln --symbolic --force "${pkgs.capnproto.src}/c++/src/capnp/compat/http-over-capnp.h" src/capnp/compat/
    ln --symbolic --force "${pkgs.capnproto.src}/c++/src/capnp/compat/http-over-capnp.c++" src/capnp/compat/
  '';

  aeron-cpp = import ./aeron-cpp.nix { inherit pkgs; };
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
