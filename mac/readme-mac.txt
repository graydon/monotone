Steps to build a distributable OS X dmg:

1) Install the prerequisites listed in INSTALL, be sure to build
gettext so that translations work. Static libraries should be built
for libintl.a and the Boost libraries, and they need to be built as
universal binaries.
Make also sure you've TeX installed, as we're bundling a PDF version
of the monotone documentation later on. The easiest way to install TeX
on Mac OS X is using the gwTeX i-Install package; further instructions for
this can be found here: http://ii2.sourceforge.net/tex-index.html. 
After installation the all the needed binaries can be found in
/usr/local/gwTeX/bin/i386-apple-darwin-current.


At the time of writing Macports doesn't build these as universal binaries.

2) Configure it (changing paths appropriately):
   mkdir "build"
   ../configure \
        --with-libintl-prefix=/usr/local/stow/gettext-0.16.1 \
        CFLAGS="-O2 -mdynamic-no-pic -ggdb -gfull -isysroot /Developer/SDKs/MacOSX10.4u.sdk -arch ppc -arch i386" \
        CXXFLAGS="-O2 -mdynamic-no-pic -fno-threadsafe-statics -ggdb -gfull -isysroot /Developer/SDKs/MacOSX10.4u.sdk -arch ppc -arch i386" \
        CPPFLAGS="-pipe -I/usr/local/stow/boost-1.33.1-fat/include" LDFLAGS="-L/usr/local/stow/boost-1.33.1-fat/lib -dead_strip" \
        STRIP="stripS" \
        --disable-dependency-tracking

(note that stripS is a wrapper executable in PATH that runs "strip -S")

Edit the resultant Makefile and change the "LIBINTL = " line to something
similar (with correct path) to:
LIBINTL = /usr/local/stow/gettext-0.16.1/lib/libintl.a -framework CoreFoundation

Check that other libraries such as zlib and libiconv
seem to be pointing at system-provided paths.

3) "make" to build it

4) Run "otool -L" on the mtn binary and check that it only links against
   system-provided libraries (in /usr/lib).

   The mtn binary can be stripped with "strip -S" and is suitable for
   standalone distribution. 

4) Run "make dmg" to build a distributable installer, with working translations
   and documentation. The "mac/monotone.pmproj" is the base for the install
   package, however the version number is automatically replaced.

5) Test the installer preferably on a different system.
