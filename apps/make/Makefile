ARCH?=		i386
TARGET=		${ARCH}-elf-ananas
PREFIX?=	/usr
DESTDIR?=	$(realpath ../output.${ARCH})

VERSION?=	3.82
MIRROR?=	http://ftp.gnu.org/gnu/make/
PATCH?=		coreutils.ananas.diff

# tools to use
DOWNLOAD?=	wget
TAR?=		tar
TOUCH?=		touch

R=		${CURDIR}
CC=		${R}/../../toolchain/prefix.${ARCH}/bin/${TARGET}-gcc
LD=		${R}/../../toolchain/prefix.${ARCH}/bin/${TARGET}-ld

make:		.install

../distfiles/make-${VERSION}.tar.gz:
		(cd ../distfiles && ${DOWNLOAD} ${MIRROR}/make-${VERSION}.tar.gz)

.extract:	../distfiles/make-${VERSION}.tar.gz
		${TAR} xf ../distfiles/make-${VERSION}.tar.gz
		@${TOUCH} .extract

.patch:		.extract make.ananas.diff
		(cd make-${VERSION} && patch -p1 < ${R}/make.ananas.diff)
		@${TOUCH} .patch

.configure:	.patch
		(cd make-${VERSION} && ./configure --host=${TARGET} --prefix=${PREFIX})
		@${TOUCH} .configure

.build:		.configure
		(cd make-${VERSION} && make)
		@${TOUCH} .build

.install:	.build
		(cd make-${VERSION} && make install DESTDIR=${DESTDIR})

clean:
		rm -rf make-${VERSION}
		rm -f .extract .patch .configure .build
