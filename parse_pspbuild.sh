#!/bin/bash
# parse_pspbuild.sh by Wouter Wijsman (wwijsman@live.nl)

source "${1}"

case "${2}" in
"depends")
    echo "${depends[@]} ${makedepends[@]}"
    ;;
"provides")
    echo "${pkgname} ${provides[@]}"
    ;;
"conflicts")
    echo "${conflicts[@]}"
    ;;
"pkgname")
    echo "${pkgname}"
    ;;
"pkgoutput")
    echo "${pkgname}-${pkgver}-${pkgrel}-${arch}.pkg.tar.gz"
    ;;
esac

