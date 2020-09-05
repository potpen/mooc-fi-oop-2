##############################################################################
# LuaJIT top level Makefile for installation. Requires GNU Make.
#
# Please read doc/install.html before changing any variables!
#
# Suitable for POSIX platforms (Linux, *BSD, OSX etc.).
# Note: src/Makefile has many more configurable options.
#
# ##### This Makefile is NOT useful for Windows! #####
# For MSVC, please follow the instructions given in src/msvcbuild.bat.
# For MinGW and Cygwin, cd to src and run make with the Makefile there.
#
# Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
##############################################################################

MAJVER=  2
MINVER=  1
RELVER=  0
PREREL=  -beta3
VERSION= $(MAJVER).$(MINVER).$(RELVER)$(PREREL)
ABIVER=  5.1

##############################################################################
#
# Change the installation path as needed. This automatically adjusts
# the paths in src/luaconf.h, too. Note: PREFIX must be an absolute path!
#
export PREFIX= /usr/local
export MULTILIB= lib
##############################################################################

DPREFIX= $(DESTDIR)$(PREFIX)
INSTALL_BIN=   $(DPREFIX)/bin
INSTALL_LIB=   $(DPREFIX)/$(MULTILIB)
INSTALL_SHARE= $(DPREFIX)/share
INSTALL_DE