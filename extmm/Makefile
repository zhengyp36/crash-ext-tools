.PHONY: all clean install uninstall

ifeq ($(shell uname -p),x86_64)
CPU_TYPE = X86_64
else
ifeq ($(shell uname -p),aarch64)
CPU_TYPE = ARM
else
CPU_TYPE = 
endif
endif

DIR_SRC      = src
DIR_RELEASE  = release
DIR_INC      = inc
DIR_AUTO_INC = inc/auto
DIR_INSTALL  = /usr/lib64/crash/extensions

DIR_EXTPY_INSTALL = /usr/local/extpy
DIR_C_CODE_INSTALL = /usr/local/extpy/src
DIR_AUTO_INC_INSTALL = $(DIR_EXTPY_INSTALL)/inc/auto

CRASH_VERSION = $(shell crash -v | grep ^crash | awk '{print $$2}' | cut -d '-' -f 1)
CRASH_CFLAGS += -I../extpy/inc/$(CRASH_VERSION) -D$(CPU_TYPE)

CFLAGS += -g -rdynamic -std=gnu99 -Wall -Werror -I$(DIR_INC) -I$(DIR_INC)/adapt

CRASH_SO += $(DIR_RELEASE)/runso.so
INSTALL_SRCS += ls_zvol.c
INSTALL_SRCS += rwlock.c

BINS += $(DIR_RELEASE)/debug
BINS += $(CRASH_SO)

ifeq ($(COMPILE_DEMO),y)
BINS += $(DIR_RELEASE)/demo.so
endif

all: $(BINS)

clean:
	$(RM) $(BINS) -r $(DIR_RELEASE)

install: $(CRASH_SO)
	@ret=0;								\
	for f in $(CRASH_SO); do					\
		install -v $$f $(DIR_INSTALL);				\
		if [ $$? -ne 0 ]; then					\
			ret=1;						\
			break;						\
		fi;							\
	done; [ $$ret -eq 0 ]
	@mkdir -p $(DIR_C_CODE_INSTALL)
	@ret=0;								\
	for f in $(INSTALL_SRCS); do					\
		/bin/cp -v $(DIR_SRC)/$$f $(DIR_C_CODE_INSTALL);	\
		if [ $$? -ne 0 ]; then					\
			ret=1;						\
			break;						\
		fi;							\
	done; [ $$ret -eq 0 ]
	@cp -rfv $(DIR_INC) $(DIR_EXTPY_INSTALL)/
	@cp -rfv Makefile $(DIR_EXTPY_INSTALL)/
	@mkdir -p $(DIR_EXTPY_INSTALL)/$(DIR_SRC)

uninstall:
	@for f in $(CRASH_SO); do					\
		rm -rfv $(DIR_INSTALL)/$$f;				\
	done
	@rm -rfv $(DIR_EXTPY_INSTALL)/$(DIR_INC)
	@rm -rfv $(DIR_EXTPY_INSTALL)/$(DIR_SRC)
	@rm -rfv $(DIR_EXTPY_INSTALL)/Makefile

$(DIR_RELEASE)/demo.so: $(DIR_SRC)/demo.c $(DIR_AUTO_INC)/zfs_crash.h

$(DIR_RELEASE)/debug: $(DIR_SRC)/debug.c $(DIR_SRC)/extmm.c $(DIR_SRC)/avl.c
	@mkdir -p $$(dirname $@)
	@echo " CC $@"
	@$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(DIR_RELEASE)/runso.so: $(DIR_SRC)/runso.c $(DIR_SRC)/crash_intf.c $(DIR_SRC)/extmm.c $(DIR_SRC)/avl.c
	@mkdir -p $$(dirname $@)
	@echo " CC $@"
	@$(CC) -shared -fPIC $(CFLAGS) $(CRASH_CFLAGS) $(filter %.c,$^) -o $@

$(DIR_RELEASE)/%.so:
	@mkdir -p $$(dirname $@)
	@echo " CC $@"
	@$(CC) -shared -fPIC $(CFLAGS) $(filter-out %.h,$^) -o $@

$(DIR_RELEASE)/%.zfs.so: $(DIR_SRC)/%.c $(DIR_AUTO_INC)/zfs_crash.h
	@mkdir -p $$(dirname $@)
	@echo " CC $@"
	@$(CC) -shared -fPIC $(CFLAGS) $(filter-out %.h,$^) -o $@
