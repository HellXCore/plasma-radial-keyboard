# Maintainer: HellXCore <mechanic.of.core@protonmail.com>
pkgname=plasma-radial-keyboard
pkgver=0.1.5
pkgrel=1
pkgdesc="Radial virtual keyboard for Wayland"
arch=('x86_64')
url="https://github.com/HellXCore/${pkgname}"
license=('GPL3')
depends=('wayland' 'cairo' 'pango')
makedepends=('wayland-protocols' 'pkgconf' 'gcc' 'make')
source=("${pkgname}-${pkgver}.tar.gz::https://github.com/HellXCore/${pkgname}/archive/refs/tags/v${pkgver}.tar.gz")
sha256sums=('SKIP')

build() {
  cd "${pkgname}-${pkgver}"
  make all
}

check() {
  cd "${pkgname}-${pkgver}"
  make test
}

package() {
  cd "${pkgname}-${pkgver}"
  make install DESTDIR="${pkgdir}" PREFIX=/usr
}
