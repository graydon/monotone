AUTOMAKE_OPTIONS=subdir-objects 1.7.1
ACLOCAL_AMFLAGS = -I m4

MOST_SOURCES =								\
	 app_state.cc commands.cc diff_patch.cc lua.cc			\
         transforms.cc update.cc work.cc cert.cc database.cc file_io.cc	\
         keys.cc manifest.cc packet.cc sanity.cc vocab.cc rcs_file.cc	\
         xdelta.cc ui.cc schema_migration.cc				\
         constants.cc netsync.cc netcmd.cc merkle_tree.cc basic_io.cc	\
         mkstemp.cc lcs.cc rcs_import.hh rcs_import.cc revision.cc	\
	 change_set.cc mt_version.cc automate.cc database_check.cc	\
	 path_component.cc epoch.cc inodeprint.cc annotate.cc           \
									\
         app_state.hh commands.hh file_io.hh manifest.hh packet.hh	\
	 sanity.hh update.hh work.hh cert.hh database.hh keys.hh	\
	 transforms.hh vocab.hh				\
	 cleanup.hh diff_patch.hh lua.hh basic_io.hh			\
	 unit_tests.hh vocab_terms.hh interner.hh cycle_detector.hh	\
         randomfile.hh rcs_file.hh xdelta.hh adler32.hh			\
         lcs.hh constants.hh ui.hh schema_migration.hh quick_alloc.hh	\
	 netsync.hh netcmd.hh netio.hh merkle_tree.hh		\
	 numeric_vocab.hh revision.hh change_set.hh			\
	 mkstemp.hh mt_version.hh automate.hh database_check.hh smap.hh	\
	 gettext.h package_revision.c package_full_revision.c           \
	 path_component.hh epoch.hh package_full_revision.h             \
	 package_revision.h inodeprint.hh annotate.hh

NETXX_SOURCES =									\
	netxx/accept.cxx netxx/accept.h netxx/address.cxx			\
	netxx/common.h netxx/compat.h netxx/datagram.cxx			\
	netxx/datagramserver.cxx netxx/osutil.cxx netxx/osutil.h		\
	netxx/peer.cxx netxx/probe.cxx netxx/probe_impl.h			\
	netxx/probe_select.cxx netxx/recvfrom.cxx netxx/recvfrom.h		\
	netxx/resolve.h netxx/serverbase.cxx netxx/serverbase.h			\
        netxx/sockaddr.cxx netxx/sockaddr.h netxx/socket.cxx			\
	netxx/socket.h netxx/sockopt.cxx netxx/stream.cxx			\
	netxx/streambase.cxx netxx/streamserver.cxx				\
	netxx/address.h netxx/datagram.h netxx/datagramserver.h			\
	netxx/netbuf.h netxx/netxx.h netxx/peer.h netxx/probe.h			\
	netxx/probeinfo.h netxx/sockopt.h netxx/stream.h netxx/streambase.h	\
	netxx/streamserver.h netxx/timeout.h netxx/types.h


CRYPTOPP_SOURCES = \
        cryptopp/algebra.cpp cryptopp/algparam.cpp cryptopp/arc4.cpp \
        cryptopp/asn.cpp cryptopp/base64.cpp cryptopp/basecode.cpp \
        cryptopp/crc.cpp cryptopp/cryptlib.cpp cryptopp/des.cpp cryptopp/dessp.cpp \
        cryptopp/dll.cpp cryptopp/eprecomp.cpp cryptopp/files.cpp cryptopp/filters.cpp \
        cryptopp/fips140.cpp cryptopp/gzip.cpp cryptopp/hex.cpp cryptopp/hmac.cpp \
        cryptopp/integer.cpp cryptopp/iterhash.cpp cryptopp/misc.cpp \
        cryptopp/modes.cpp cryptopp/mqueue.cpp cryptopp/nbtheory.cpp \
        cryptopp/oaep.cpp cryptopp/osrng.cpp cryptopp/pch.cpp cryptopp/pkcspad.cpp \
        cryptopp/pssr.cpp cryptopp/pubkey.cpp cryptopp/queue.cpp \
        cryptopp/randpool.cpp cryptopp/rng.cpp cryptopp/rsa.cpp cryptopp/sha.cpp \
        cryptopp/strciphr.cpp cryptopp/trdlocal.cpp cryptopp/zdeflate.cpp \
        cryptopp/zinflate.cpp \
	\
        cryptopp/algebra.h cryptopp/algparam.h cryptopp/arc4.h cryptopp/argnames.h \
        cryptopp/asn.h cryptopp/base64.h cryptopp/basecode.h cryptopp/config.h \
        cryptopp/crc.h cryptopp/cryptlib.h cryptopp/des.h cryptopp/dll.h cryptopp/eprecomp.h \
        cryptopp/files.h cryptopp/filters.h cryptopp/fips140.h cryptopp/fltrimpl.h \
        cryptopp/gzip.h cryptopp/hex.h cryptopp/integer.h cryptopp/iterhash.h \
        cryptopp/mdc.h cryptopp/misc.h cryptopp/modarith.h cryptopp/modes.h \
        cryptopp/mqueue.h cryptopp/nbtheory.h cryptopp/oaep.h cryptopp/oids.h \
        cryptopp/osrng.h cryptopp/pch.h cryptopp/pkcspad.h cryptopp/pssr.h \
        cryptopp/pubkey.h cryptopp/queue.h cryptopp/randpool.h cryptopp/rng.h \
        cryptopp/rsa.h cryptopp/secblock.h cryptopp/seckey.h cryptopp/sha.h \
        cryptopp/simple.h cryptopp/smartptr.h cryptopp/stdcpp.h cryptopp/strciphr.h \
        cryptopp/trdlocal.h cryptopp/words.h cryptopp/zdeflate.h \
        cryptopp/zinflate.h cryptopp/hmac.h

BOOST_SANDBOX_SOURCES = \
        boost/circular_buffer_adaptor.hpp \
	boost/circular_buffer_base.hpp \
	boost/circular_buffer_fwd.hpp \
	boost/circular_buffer.hpp

SQLITE_SOURCES = \
        sqlite/alter.c sqlite/attach.c sqlite/auth.c sqlite/btree.c \
        sqlite/build.c sqlite/date.c sqlite/delete.c sqlite/expr.c sqlite/func.c \
        sqlite/hash.c sqlite/insert.c \
        sqlite/legacy.c sqlite/main.c sqlite/opcodes.c \
        sqlite/os_unix.c sqlite/os_win.c \
        sqlite/pager.c sqlite/parse.c sqlite/pragma.c sqlite/printf.c \
        sqlite/random.c sqlite/select.c sqlite/table.c sqlite/tokenize.c \
        sqlite/trigger.c sqlite/update.c sqlite/utf.c \
        sqlite/util.c sqlite/vacuum.c \
        sqlite/vdbe.c sqlite/vdbeapi.c sqlite/vdbeaux.c \
        sqlite/vdbemem.c sqlite/where.c \
	\
        sqlite/btree.h sqlite/config.h sqlite/hash.h sqlite/opcodes.h sqlite/os.h \
        sqlite/os_common.h sqlite/os_unix.h sqlite/os_win.h \
        sqlite/parse.h sqlite/sqlite3.h sqlite/sqliteInt.h sqlite/vdbe.h sqlite/vdbeInt.h \
	sqlite/pager.h

LUA_SOURCES = \
	lua/lapi.c lua/lapi.h lua/lauxlib.c lua/lauxlib.h lua/lbaselib.c \
	lua/lcode.c lua/lcode.h lua/ldblib.c lua/ldebug.c lua/ldebug.h lua/ldo.c \
	lua/ldo.h lua/ldump.c lua/lfunc.c lua/lfunc.h lua/lgc.c lua/lgc.h \
	lua/liolib.c lua/llex.c lua/llex.h lua/llimits.h lua/lmathlib.c lua/lmem.c \
	lua/lmem.h lua/loadlib.c lua/lobject.c lua/lobject.h lua/lopcodes.c \
	lua/lopcodes.h lua/lparser.c lua/lparser.h lua/lstate.c lua/lstate.h \
	lua/lstring.c lua/lstring.h lua/lstrlib.c lua/ltable.c lua/ltable.h \
	lua/ltablib.c lua/ltests.c lua/ltm.c lua/ltm.h lua/lua.h lua/lualib.h \
	lua/lundump.c lua/lundump.h lua/lvm.c lua/lvm.h lua/lzio.c lua/lzio.h

IDNA_SOURCES = \
	idna/gunibreak.h idna/gunidecomp.h idna/idna.h idna/nfkc.c	\
	idna/punycode.c idna/rfc3454.c idna/stringprep.h idna/version.c \
	idna/gunicomp.h idna/idna.c idna/idn-int.h idna/profiles.c	\
	idna/punycode.h idna/stringprep.c idna/toutf8.c

POPT_SOURCES = \
	popt/findme.c  popt/poptconfig.c  popt/poptint.h   \
	popt/findme.h  popt/popt.h        popt/poptparse.c \
	popt/popt.c    popt/popthelp.c    popt/system.h

UNIX_PLATFORM_SOURCES = \
	unix/read_password.cc unix/get_system_flavour.cc unix/process.cc unix/terminal.cc \
	unix/platform_netsync.cc unix/inodeprint.cc

WIN32_PLATFORM_SOURCES = \
	win32/read_password.cc win32/get_system_flavour.cc win32/process.cc win32/terminal.cc \
	win32/platform_netsync.cc win32/inodeprint.cc

# primaries

bin_PROGRAMS = monotone
check_PROGRAMS = unit_tests
noinst_PROGRAMS = txt2c 

monotone_SOURCES = $(MOST_SOURCES) monotone.cc main.cc
unit_tests_SOURCES = $(MOST_SOURCES) unit_tests.cc

txt2c_SOURCES = txt2c.cc

noinst_LIBRARIES = libplatform.a lib3rdparty.a 
libplatform_a_SOURCES = platform.hh
lib3rdparty_a_SOURCES = $(BOOST_SANDBOX_SOURCES) \
			$(CRYPTOPP_SOURCES) \
			$(IDNA_SOURCES) \
			$(POPT_SOURCES) \
			$(NETXX_SOURCES)

# flags

AM_CPPFLAGS =
AM_CFLAGS = $(AM_CPPFLAGS) -DTEMP_STORE=1 -DNDEBUG -DBOOST_DISABLE_THREADS -DSQLITE_OMIT_CURSOR -Wall $(SQLITE_CPPFLAGS)
AM_CXXFLAGS = $(AM_CPPFLAGS) -DNDEBUG -DBOOST_DISABLE_THREADS -Wall 
AM_LDFLAGS = 

lib3rdparty_a_CPPFLAGS =

monotone_LDFLAGS = 
monotone_CPPFLAGS = 

unit_tests_LDFLAGS = 
unit_tests_CPPFLAGS = -DBUILD_UNIT_TESTS

# conditionals from configury

if BUNDLED_LUA
  lib3rdparty_a_SOURCES += $(LUA_SOURCES)
  AM_CPPFLAGS += -I$(top_srcdir)/lua
endif

if BUNDLED_SQLITE
  lib3rdparty_a_SOURCES += $(SQLITE_SOURCES)
  AM_CPPFLAGS += -I$(top_srcdir)/sqlite
endif

if STATIC_BOOST
  BOOSTLIBS = @BOOST_LIBDIR@/libboost_filesystem$(BOOST_SUFFIX).a \
              @BOOST_LIBDIR@/libboost_date_time$(BOOST_SUFFIX).a \
              @BOOST_LIBDIR@/libboost_regex$(BOOST_SUFFIX).a
  monotone_LDADD = lib3rdparty.a $(BOOSTLIBS)
  unit_tests_LDADD = lib3rdparty.a $(BOOSTLIBS) \
                     @BOOST_LIBDIR@/libboost_unit_test_framework$(BOOST_SUFFIX).a
else
  monotone_LDADD = lib3rdparty.a
  unit_tests_LDADD = lib3rdparty.a -lboost_unit_test_framework$(BOOST_SUFFIX)
endif

monotone_LDADD += libplatform.a $(LIBICONV) $(LIBINTL)
unit_tests_LDADD += libplatform.a $(LIBICONV) $(LIBINTL)

if WIN32_PLATFORM
  libplatform_a_SOURCES += $(WIN32_PLATFORM_SOURCES)
  monotone_LDADD += -lws2_32 -lintl -liconv -liphlpapi
  unit_tests_LDADD += -lws2_32 -lintl -liconv -liphlpapi
else
  libplatform_a_SOURCES += $(UNIX_PLATFORM_SOURCES)
endif

if MISSING_INET_PTON
  lib3rdparty_a_SOURCES += netxx/inet_pton.cxx netxx/inet_pton.h
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_PTON
endif    

if MISSING_INET_NTOP
  lib3rdparty_a_SOURCES += netxx/inet_ntop.cxx netxx/inet_ntop.h
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_NTOP
endif    

if MISSING_GETADDRINFO
  lib3rdparty_a_SOURCES += netxx/resolve_gethostbyname.cxx netxx/resolve_getservbyname.cxx
else
  lib3rdparty_a_SOURCES += netxx/resolve_getaddrinfo.cxx
endif    

if MISSING_INET6
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_INET6
endif

# i18n support

SUBDIRS = po
datadir = @datadir@
localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

# extra files

PDF_FIGURES= figures/branch-heads.pdf figures/cert.pdf				\
	figures/difference-between-versions.pdf					\
	figures/file-id-manifest-id.pdf figures/fork.pdf			\
	figures/general-workflow.pdf figures/linear-history.pdf			\
	figures/local-workflow.pdf figures/manifest.pdf figures/merge.pdf	\
	figures/network-workflow.pdf figures/parent-child-hashes.pdf		\
	figures/parent-child-names-hashes.pdf figures/parent-child.pdf		\
	figures/revision-chaining.pdf figures/revision.pdf			\
	figures/statement.pdf figures/three-versions.pdf			\
	figures/two-branches.pdf

PNG_FIGURES=$(addprefix $(top_builddir)/figures/,$(notdir $(PDF_FIGURES:.pdf=.png)))
EPS_FIGURES=$(addprefix $(top_builddir)/figures/,$(notdir $(PDF_FIGURES:.pdf=.eps)))

man1_MANS = monotone.1
info_TEXINFOS = monotone.texi
EXTRA_DIST = $(man1_MANS) \
	     $(PDF_FIGURES) \
	     $(PNG_FIGURES) \
             INSTALL \
             README.changesets \
             UPGRADE \
             monotone.spec \
             monotone.html \
             texinfo.css \
	     schema.sql views.sql \
	     std_hooks.lua test_hooks.lua \
	     testsuite.at \
	     $(wildcard $(srcdir)/tests/t_*.at) \
	     testsuite \
             tests/atconfig.in \
             tests/atlocal.in  \
	     package.m4 \
	     package_revision.txt package_full_revision_dist.txt \
             $(wildcard $(srcdir)/m4/*.m4) \
             popt/README popt/CHANGES popt/COPYING popt/popt.3 popt/popt.ps \
             $(wildcard $(srcdir)/popt/test?.c) popt/testit.sh \
             sqlite/keywordhash.h \
             contrib/README \
             contrib/monotone-notify.pl \
             contrib/monotone-import.pl \
             contrib/ciabot_monotone.py \
             contrib/monotone.el \
             contrib/monotone.bash_completion \
             win32/monotone.iss \
             $(wildcard $(srcdir)/debian/*)

MAKEINFOFLAGS=-I $(top_builddir)

monotone.pdf: monotone.texi $(PDF_FIGURES)

monotone.dvi: monotone.texi $(EPS_FIGURES)

%.eps: %.epsi
	mv $< $@

%.epsi: %.ps
	ps2epsi $< $@

$(top_builddir)/figures/%.ps: $(srcdir)/figures/%.pdf
	mkdir -p $(top_builddir)/figures
#	acroread -toPostScript -pairs $< $@
	pdftops $< $@


# testsuite stuff (could this possibly be more ugly?)

TESTS = unit_tests $(srcdir)/testsuite

mostlyclean-local:
	rm -rf testsuite.dir testsuite.log package_full_revision_dist.txt

DISTCLEANFILES = tests/atconfig mt-stdint.h

# distcheck stuff

DISTCHECK_CONFIGURE_FLAGS = \
			 --with-bundled-sqlite \
			 --with-bundled-lua \
			 --enable-static-boost

# note: these targets are all in srcdir; they are *not* built during
# the VPATH build part of distcheck (with non-writable source
# directory) but only during the initial dist stage.

$(srcdir)/package.m4: $(top_srcdir)/configure.ac
	{                                      \
	  echo '# Signature of the current package.'; \
	  echo 'm4_define([AT_PACKAGE_NAME],      [@PACKAGE_NAME@])'; \
	  echo 'm4_define([AT_PACKAGE_TARNAME],   [@PACKAGE_TARNAME@])'; \
	  echo 'm4_define([AT_PACKAGE_VERSION],   [@PACKAGE_VERSION@])'; \
	  echo 'm4_define([AT_PACKAGE_STRING],    [@PACKAGE_STRING@])'; \
	  echo 'm4_define([AT_PACKAGE_BUGREPORT], [@PACKAGE_BUGREPORT@])'; \
	} >$(srcdir)/package.m4

$(srcdir)/testsuite: $(srcdir)/testsuite.at $(wildcard $(srcdir)/tests/t_*.at) \
                     $(srcdir)/package.m4 
	autom4te --language=Autotest -I $(srcdir) $< -o $@.tmp
	mv $@.tmp $@

# we generate some headers to copy data into the executable

BUILT_SOURCES_CLEAN = std_hooks.h test_hooks.h schema.h views.h \
                      package_revision.txt package_revision.c \
                      package_full_revision.txt package_full_revision.c
BUILT_SOURCES_NOCLEAN = package_revision.txt package_full_revision_dist.txt
BUILT_SOURCES = $(BUILT_SOURCES_CLEAN) $(BUILT_SOURCES_NOCLEAN)
CLEANFILES = $(BUILT_SOURCES_CLEAN) $(EPS_FIGURES)

txt2c: txt2c.cc Makefile
	$(CXX) -Wall -o $@ $<
	chmod 0755 $@$(EXEEXT)

apidocs:
	doxygen doxygen.cfg

%.h: %.sql txt2c Makefile
	./txt2c $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv $@.tmp $@
	rm -f $@.tmp

%.h: %.lua txt2c Makefile
	./txt2c $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv $@.tmp $@
	rm -f $@.tmp

# A fake target so we can use MT/revision as a prerequisite, yet not
# crash if MT/revision does not exist.
$(srcdir)/MT/revision:
	true
package_revision.txt: $(srcdir)/MT/revision
	if [ -f $< ]; then cp $< $@; fi
	if [ ! -f $@ ]; then echo "unknown" > $@; fi
package_revision.c: package_revision.txt txt2c Makefile
	./txt2c --strip-trailing --no-static $< package_revision >$@.tmp
	cmp -s $@.tmp $@ || mv $@.tmp $@
	rm -f $@.tmp

.PHONY: package_full_revision.txt apidocs
package_full_revision.txt:
	REAL_BLDDIR=$$PWD/$(top_builddir); \
	(cd $(srcdir) && $$REAL_BLDDIR/monotone status) 2>/dev/null >$@ \
	|| (cd $(srcdir) && monotone status) 2>/dev/null >$@ \
	|| (cp $(srcdir)/package_full_revision_dist.txt 2>/dev/null $@) \
	|| (echo "unknown" >$@)
package_full_revision_dist.txt: package_full_revision.txt
	cp $< $@
	(echo ''; \
	 echo '  Generated from data cached in the distribution;'; \
	 echo '  further changes may have been made.') >> $@
package_full_revision.c: package_full_revision.txt txt2c Makefile
	./txt2c --no-static $< package_full_revision >$@.tmp
	cmp -s $@.tmp $@ || mv $@.tmp $@
	rm -f $@.tmp

# automake doesn't build html docs

monotone.html: monotone.texi texinfo.css
	makeinfo --no-split --no-headers --output $@ --html $<
	perl -i.perlbak -pe 's@<head>@<head>\n<link type="text/css" rel="stylesheet" href="texinfo.css" />@' $@
	rm -f monotone.html.perlbak

# The .perlbak thing is needed, otherwise the perl executions fails on MinGW

html: monotone.texi texinfo.css
	mkdir -p html
	makeinfo --number-sections --html --output html $<
	perl -i.perlbak -pe 's@<head>@<head>\n<link type="text/css" rel="stylesheet" href="texinfo.css" />@' html/*.html
	rm -f html/*.perlbak
	cp texinfo.css html
