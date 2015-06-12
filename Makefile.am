ACLOCAL_AMFLAGS = -I m4
AM_DEFAULT_SOURCE_EXT = .cpp
AM_CPPFLAGS = $(DYPLO_CFLAGS)
AM_LDFLAGS = $(DYPLO_LIBS) $(PTHREAD_CFLAGS) $(PTHREAD_LIBS)

bin_PROGRAMS = dyploexampleapphw dyploexampleappsw

dyploexampleappsw_SOURCES = main.cpp softwareprocesses.hpp
dyploexampleapphw_SOURCES = main.cpp softwareprocesses.hpp

dyploexampleapphw_CPPFLAGS = $(DYPLO_CFLAGS) -DHAVE_HARDWARE
