AUTOMAKE_OPTIONS=subdir-objects

MOST_SOURCES = \
	 app_state.cc commands.cc diff_patch.cc lua.cc              \
	 nntp_tasks.cc patch_set.cc transforms.cc update.cc work.cc \
	 cert.cc database.cc file_io.cc keys.cc manifest.cc         \
         nntp_machine.cc packet.cc sanity.cc vocab.cc               \
         rcs_file.cc rcs_import.cc xdelta.cc network.cc             \
         http_tasks.cc ui.cc schema_migration.cc                    \
                                                                    \
         app_state.hh commands.hh file_io.hh manifest.hh packet.hh  \
	 sanity.hh update.hh work.hh cert.hh database.hh keys.hh    \
	 nntp_machine.hh packet_types.hh transforms.hh vocab.hh     \
	 cleanup.hh diff_patch.hh lua.hh nntp_tasks.hh patch_set.hh \
	 unit_tests.hh vocab_terms.hh interner.hh cycle_detector.hh \
         rcs_file.hh rcs_import.hh xdelta.hh adler32.hh lcs.hh      \
         network.hh http_tasks.hh constants.hh ui.hh schema_migration.hh 

CRYPTOPP_SOURCES = \
        cryptopp/algebra.cpp cryptopp/algparam.cpp cryptopp/arc4.cpp \
        cryptopp/asn.cpp cryptopp/base64.cpp cryptopp/basecode.cpp \
        cryptopp/crc.cpp cryptopp/cryptlib.cpp cryptopp/des.cpp cryptopp/dessp.cpp \
        cryptopp/eprecomp.cpp cryptopp/files.cpp cryptopp/filters.cpp \
        cryptopp/fips140.cpp cryptopp/gzip.cpp cryptopp/hex.cpp \
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
        cryptopp/crc.h cryptopp/cryptlib.h cryptopp/des.h cryptopp/eprecomp.h \
        cryptopp/files.h cryptopp/filters.h cryptopp/fips140.h cryptopp/fltrimpl.h \
        cryptopp/gzip.h cryptopp/hex.h cryptopp/integer.h cryptopp/iterhash.h \
        cryptopp/mdc.h cryptopp/misc.h cryptopp/modarith.h cryptopp/modes.h \
        cryptopp/mqueue.h cryptopp/nbtheory.h cryptopp/oaep.h cryptopp/oids.h \
        cryptopp/osrng.h cryptopp/pch.h cryptopp/pkcspad.h cryptopp/pssr.h \
        cryptopp/pubkey.h cryptopp/queue.h cryptopp/randpool.h cryptopp/rng.h \
        cryptopp/rsa.h cryptopp/secblock.h cryptopp/seckey.h cryptopp/sha.h \
        cryptopp/simple.h cryptopp/smartptr.h cryptopp/strciphr.h \
        cryptopp/trdlocal.h cryptopp/words.h cryptopp/zdeflate.h \
        cryptopp/zinflate.h cryptopp/hmac.h

BOOST_SANDBOX_SOURCES = \
	boost/socket/src/impl/address_storage.cpp \
	boost/socket/src/impl/impl.cpp \
	boost/socket/src/impl/socket_init.cpp \
	boost/socket/src/ip4/address.cpp \
	boost/socket/src/ip4/protocol.cpp \
	boost/socket/src/address_info.cpp \
	boost/socket/src/any_address.cpp \
	boost/socket/src/default_socket_impl.cpp \
	boost/socket/src/interface.cpp \
	boost/socket/src/socket_option.cpp \
	\
        boost/socket/concept/address.hpp \
        boost/socket/concept/error_policy.hpp \
        boost/socket/concept/protocol.hpp \
        boost/socket/acceptor_socket.hpp \
        boost/socket/address_info.hpp \
        boost/socket/any_address.hpp \
        boost/socket/any_protocol.hpp \
        boost/socket/asynch_acceptor.hpp \
        boost/socket/asynch_data_socket.hpp \
        boost/socket/asynch_socket_base.hpp \
        boost/socket/config.hpp \
        boost/socket/connector_socket.hpp \
        boost/socket/data_socket.hpp \
        boost/socket/interface.hpp \
        boost/socket/ip4.hpp \
        boost/socket/proactor.hpp \
        boost/socket/socket_base.hpp \
        boost/socket/socket_errors.hpp \
        boost/socket/socket_exception.hpp \
        boost/socket/socket_option.hpp \
        boost/socket/socket_set.hpp \
        boost/socket/socket_streambuf.hpp \
        boost/socket/socketstream.hpp \
        boost/socket/time_value.hpp \
        boost/socket/impl/address_storage.hpp \
        boost/socket/impl/default_asynch_socket_impl.hpp \
        boost/socket/impl/default_error_policy.hpp \
        boost/socket/impl/default_socket_impl.hpp \
        boost/socket/impl/default_socket_proactor.hpp \
        boost/socket/impl/socket_init.hpp \
        boost/socket/ip4/address.hpp \
        boost/socket/ip4/protocol.hpp \
        boost/socket/src/impl/win32/overlapped.hpp \
        boost/io/streambuf_wrapping.hpp 

# warning: we're *not* currently building on win32. we'll figure out
# how to do that someday, I suspect, but not today.
#
# BOOST_SANDBOX_WIN32_SOURCES = \
# 	boost/socket/src/impl/win32/default_asynch_socket_impl.cpp \
# 	boost/socket/src/impl/win32/default_socket_proactor.cpp 


SQLITE_SOURCES = \
        sqlite/attach.c sqlite/auth.c sqlite/btree.c sqlite/btree_rb.c \
        sqlite/build.c sqlite/copy.c sqlite/delete.c sqlite/expr.c sqlite/func.c \
        sqlite/hash.c sqlite/insert.c sqlite/main.c sqlite/opcodes.c sqlite/os.c \
        sqlite/pager.c sqlite/parse.c sqlite/pragma.c sqlite/printf.c \
        sqlite/random.c sqlite/select.c sqlite/table.c sqlite/tokenize.c \
        sqlite/trigger.c sqlite/update.c sqlite/util.c sqlite/vacuum.c \
        sqlite/vdbe.c sqlite/where.c \
	\
        sqlite/btree.h sqlite/config.h sqlite/hash.h sqlite/opcodes.h sqlite/os.h \
        sqlite/parse.h sqlite/sqlite.h sqlite/sqliteInt.h sqlite/vdbe.h sqlite/pager.h

bin_PROGRAMS = monotone depot.cgi
check_PROGRAMS = unit_tests
noinst_PROGRAMS = txt2c 

monotone_SOURCES = $(MOST_SOURCES) monotone.cc
unit_tests_SOURCES = $(MOST_SOURCES) unit_tests.cc
depot_cgi_SOURCES = depot.cc 
txt2c_SOURCES = txt2c.cc

noinst_LIBRARIES = lib3rdparty.a 
lib3rdparty_a_SOURCES = $(CRYPTOPP_SOURCES) $(SQLITE_SOURCES) $(BOOST_SANDBOX_SOURCES)

monotone_LDADD = lib3rdparty.a -lboost_prg_exec_monitor
monotone_LDFLAGS = 
monotone_CPPFLAGS = 

unit_tests_LDADD = lib3rdparty.a -lboost_unit_test_framework 
unit_tests_LDFLAGS = 
unit_tests_CPPFLAGS = -DBUILD_UNIT_TESTS

depot_cgi_LDFLAGS = 
depot_cgi_LDADD = schema_migration.o lib3rdparty.a

AM_CFLAGS = -DTEMP_STORE=1 -DNDEBUG -DBOOST_DISABLE_THREADS -Wall 
AM_LDFLAGS = -static 
AM_CXXFLAGS = -DNDEBUG -DBOOST_DISABLE_THREADS -Wall 

man1_MANS = monotone.1
info_TEXINFOS = monotone.texi
EXTRA_DIST = $(man1_MANS) \
	     schema.sql depot_schema.sql \
	     std_hooks.lua test_hooks.lua \
	     testsuite.at testsuite \
	     $(wildcard tests/t_*.at) \
             tests/atconfig.in \
             tests/atlocal.in  \
	     package.m4

# testsuite stuff (could this possibly be more ugly?)

TESTS = unit_tests $(srcdir)/testsuite

MOSTLYCLEANFILES = \
	testsuite.dir/at-check-line \
	testsuite.dir/at-stdout \
	testsuite.dir/at-stder1 \
	testsuite.dir/at-stderr \
	testsuite.dir/at-times \
	testsuite.log

DISTCLEANFILES = tests/atconfig

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
                     $(srcdir)/package.m4 tests/atconfig tests/atlocal
	autom4te --language=Autotest -I $(srcdir) $< -o $@.tmp
	mv $@.tmp $@

# we generate some headers to copy data into the executable

BUILT_SOURCES = std_hooks.h test_hooks.h schema.h depot_schema.h
CLEANFILES = $(BUILT_SOURCES)

txt2c: txt2c.cc Makefile
	$(CXX) -Wall -o $@ $<
	chmod 0755 $@

%.h: %.sql txt2c Makefile
	./txt2c $< $(*F) >$@

%.h: %.lua txt2c Makefile
	./txt2c $< $(*F) >$@

# automake doesn't build html docs

html: monotone.texi 
	mkdir -p html
	makeinfo --number-sections --html --output html $<
