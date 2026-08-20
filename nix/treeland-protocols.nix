{ stdenv, lib }:

stdenv.mkDerivation rec {
  pname = "treeland-protocols";
  version = "0.5.9";

  src = ./..;

  dontBuild = true;

  installPhase = ''
    mkdir -p $out/share/treeland-protocols \
             $out/share/cmake/TreelandProtocols \
             $out/share/pkgconfig

    # Install protocol XML files
    cp $src/protocols/treeland/*.xml $out/share/treeland-protocols/

    # Generate CMake config
    cat > $out/share/cmake/TreelandProtocols/TreelandProtocolsConfig.cmake <<EOF
set(TREELAND_PROTOCOLS_DATA_DIR $out/share/treeland-protocols/)
set(TreelandProtocols_VERSION ${version})
set(TREELAND_PROTOCOLS_VERSION ${version})
EOF

    cat > $out/share/cmake/TreelandProtocols/TreelandProtocolsConfigVersion.cmake <<EOF
set(PACKAGE_VERSION ${version})
if("\${PACKAGE_VERSION}" VERSION_LESS "\${PACKAGE_FIND_VERSION}")
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
  if("\${PACKAGE_VERSION}" VERSION_EQUAL "\${PACKAGE_FIND_VERSION}")
    set(PACKAGE_VERSION_EXACT TRUE)
  endif()
endif()
EOF

    # Generate pkg-config
    cat > $out/share/pkgconfig/treeland-protocols.pc <<EOF
prefix=$out
datarootdir=\${prefix}/share
pkgdatadir=\${pc_sysrootdir}\${datarootdir}/treeland-protocols/

Name: Treeland Protocols
Description: Treeland protocol files
Version: ${version}
EOF
  '';

  meta = {
    description = "Private Wayland protocols used by treeland (bundled for nix build)";
    homepage = "https://github.com/linuxdeepin/treeland-protocols";
    license = with lib.licenses; [ mit ];
    platforms = lib.platforms.linux;
  };
}