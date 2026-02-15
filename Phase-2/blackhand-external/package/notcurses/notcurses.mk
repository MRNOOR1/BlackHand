################################################################################
# notcurses
################################################################################

NOTCURSES_VERSION = 3.0.11
NOTCURSES_SITE = https://github.com/dankamongmen/notcurses/archive/refs/tags
NOTCURSES_SOURCE = v$(NOTCURSES_VERSION).tar.gz
NOTCURSES_INSTALL_STAGING = YES
NOTCURSES_LICENSE = Apache-2.0
NOTCURSES_LICENSE_FILES = COPYRIGHT

NOTCURSES_DEPENDENCIES = ncurses libunistring libdeflate

# Build only the core C library, no multimedia, no C++, no executables,
# no docs, no tests, no proof-of-concept binaries.
NOTCURSES_CONF_OPTS = \
	-DUSE_CXX=OFF \
	-DUSE_MULTIMEDIA=none \
	-DUSE_PANDOC=OFF \
	-DUSE_DOXYGEN=OFF \
	-DUSE_GPM=OFF \
	-DUSE_QRCODEGEN=OFF \
	-DUSE_POC=OFF \
	-DUSE_DOCTEST=OFF \
	-DBUILD_EXECUTABLES=OFF \
	-DBUILD_FFI_LIBRARY=OFF \
	-DBUILD_TESTING=OFF \
	-DUSE_STATIC=OFF \
	-DUSE_DEFLATE=ON

$(eval $(cmake-package))
