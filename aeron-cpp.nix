{
  pkgs
}:

let
  version = "1.40.0";

  gradleDist = builtins.fetchurl {
    url = https://services.gradle.org/distributions/gradle-7.5.1-all.zip;
    sha256 = "06923j2cri0ay3knkqv5csrrwvqri46yhs9c55h1zxk3xl8q576v";
  };

  gradleProperties = builtins.toFile "gradle.properties" ''
    org.gradle.caching=false
    org.gradle.daemon=false
    org.gradle.debug=false
    org.gradle.vfs.watch=false
  '';

  jdk = pkgs.openjdk_headless;

in

pkgs.stdenv.mkDerivation {

  pname = "aeron-cpp";
  inherit version;

  src = pkgs.fetchFromGitHub {
    owner = "real-logic";
    repo = "aeron";
    rev = "1cda80dbcd346ee0409fec5328956292614460df";
    hash = "sha256-4C5YofA/wxwa7bfc6IqsDrw8CLQWKoVBCIe8Ec7ifAg=";
  };

  patches = [
    ./gradle.patch
  ];

  buildInputs = with pkgs; [
    libbsd
    libuuid
    zlib
  ];

  nativeBuildInputs = with pkgs; [
    # patch binaries with correct shared library rpaths
    autoPatchelfHook
    cmake
    gmock
    jdk
    makeWrapper
    patchelf
  ];

  configurePhase = ''
    ln -s --verbose "${gradleDist}" ./gradle/wrapper/gradle-all.zip

    mkdir --parents cppbuild/Release
    (
      cd cppbuild/Release
      cmake \
        -G "CodeBlocks - Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DAERON_TESTS=OFF \
        -DAERON_SYSTEM_TESTS=OFF \
        -DAERON_BUILD_SAMPLES=OFF \
        -DBUILD_AERON_ARCHIVE_API=OFF \
        -DCMAKE_INSTALL_PREFIX:PATH=../../install \
        ../..
    )
    mkdir --parents .gradle
    export GRADLE_USER_HOME="$(pwd)/.gradle"
    ln -s "${gradleProperties}" .gradle/gradle.properties
 
    # force gradlew wrapper to download distribution
    #patchShebangs --build ./gradlew
    #sh ./gradlew --version
  '';

  buildPhase = ''
    export GRADLE_USER_HOME="$(pwd)/.gradle"

    (
      cd cppbuild/Release

      make -j $NIX_BUILD_CORES \
        aeron \
        aeron_client_shared \
        aeron_driver \
        aeron_client \
        aeron_driver_static \
        aeronmd

      make -j $NIX_BUILD_CORES install
    )
  '';

  dontPatchELF = true;
  autoPatchelfIgnoreMissingDeps = [ "*" ];

  installPhase = with pkgs; ''
     mkdir --parents "$out"
     cp --archive --verbose --target-directory="$out" install/*
  '';

  meta = {
    description = "Aeron Messaging C++ Library";
    homepage = "https://github.com/real-logic/aeron";
  };
}

