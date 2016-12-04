APXS = apxs
APR_CONFIG = apr-config
APU_CONFIG = apu-config
LIBTOOL = $(shell $(APXS) -q LIBTOOL)
MOZART2_SRC_DIR = ../mozart2
MOZART2_BUILD_DIR = $(MOZART2_SRC_DIR)/build
MOZART2_INCLUDES = -I$(MOZART2_SRC_DIR)/vm/vm/main -I$(MOZART2_BUILD_DIR)/vm/vm/main/generated -I$(MOZART2_SRC_DIR)/vm/boostenv/main -I$(MOZART2_BUILD_DIR)/vm/boostenv/main/generated 
MOZART2_LIBS = $(MOZART2_BUILD_DIR)/vm/boostenv/main/libmozartvmboost.a $(MOZART2_BUILD_DIR)/vm/vm/main/libmozartvm.a -lboost_filesystem -lboost_system -lpthread -lboost_thread -lboost_random
exp_CPPFLAGS = -I$(shell $(APXS) -q exp_includedir)
exp_LDFLAGS = -L$(shell $(APXS) -q exp_libdir)
exp_libexecdir = $(shell $(APXS) -q exp_libexecdir)
apr_CPPFLAGS = -I$(shell $(APR_CONFIG) --includedir) $(shell $(APR_CONFIG) --cppflags)
apr_LDFLAGS = -L$(shell $(APR_CONFIG) --libdir)
apu_CPPFLAGS = -I$(shell $(APU_CONFIG) --includedir)
apu_LDFLAGS = -L$(shell $(APU_CONFIG) --libdir)

all: mod_wozozo.la

clean:
	rm -rf *.lo *.la *.slo *.o .libs

mod_wozozo.lo: mod_wozozo.cc
	$(LIBTOOL) --mode=compile $(CXX) -c -s $(apr_CPPFLAGS) $(apu_CPPFLAGS) $(exp_CPPFLAGS) $(MOZART2_INCLUDES) $(CPPFLAGS) $^

mod_wozozo.la: mod_wozozo.lo
	$(LIBTOOL) --mode=link $(CXX) -shared -module -avoid-version -rpath $(exp_libexecdir) -Wl,-soname -Wl,mod_wozozo.so -o $@ $^ $(MOZART2_LIBS)

.PHONY: all
