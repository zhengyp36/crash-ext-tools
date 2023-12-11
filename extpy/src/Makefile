.PHONY: all clean install uninstall dump_args check_args

INSTALL_DIR    = /usr/lib64/crash/extensions
INSTALL_PYDIR  = /usr/local/extpy
INSTALL_PY_LIB = $(INSTALL_PYDIR)/lib
INSTALL_PY_BIN = $(INSTALL_PYDIR)/bin
INSTALL_GDB    = $(INSTALL_PYDIR)/gdb
INSTALL_TXT    = $(INSTALL_PYDIR)/txt

SRC_SCRIPT     = ../scripts
CRASH_SO       = extpy.so
CRASH_PY_LIB   = extpy.py
CRASH_PY_BIN   = $(shell ls $(SRC_SCRIPT)/*.py)
CRASH_GDB      = $(shell ls $(SRC_SCRIPT)/*.gdb)
CRASH_TXT_NAME = zfs.crash.setup.txt
CRASH_TXT      = $(SRC_SCRIPT)/$(CRASH_TXT_NAME)
CRASH_ZFS      = /bin/zfscrash

all: dump_args check_args $(CRASH_SO)

check_args:
	@[ ! -z "$(CRASH_CPU_TYPE)" ] || {				\
		echo "Error: *** CRASH_CPU_TYPE is not set.";		\
		false;							\
	}
	@[ ! -z "$(CRASH_VERSION)" ] || {				\
		echo "Error: *** CRASH_VERSION is not set.";		\
		false;							\
	}

dump_args:
	@echo "CRASH_CPU_TYPE=$(CRASH_CPU_TYPE), CRASH_VERSION=$(CRASH_VERSION)"

clean:
	$(RM) $(CRASH_SO)

install: $(CRASH_SO)
	@mkdir -p $(INSTALL_DIR)
	@ret=0;								\
	for f in $(CRASH_SO); do					\
		install -v $$f $(INSTALL_DIR)/;				\
		if [ $$? -ne 0 ]; then					\
			ret=1;						\
			break;						\
		fi;							\
	done; [ $$ret -eq 0 ]
	@mkdir -p $(INSTALL_PY_LIB) &&					\
	for f in $(CRASH_PY_LIB); do					\
		install -v $$f $(INSTALL_PY_LIB);			\
	done
	@mkdir -p $(INSTALL_PY_BIN) &&					\
	for f in $(CRASH_PY_BIN); do					\
		install -v $$f $(INSTALL_PY_BIN);			\
	done
	@mkdir -p $(INSTALL_GDB) &&					\
	for f in $(CRASH_GDB); do					\
		install -v $$f $(INSTALL_GDB);				\
	done
	@mkdir -p $(INSTALL_TXT) && cp -v $(CRASH_TXT) $(INSTALL_TXT)
	@echo -e "#!/bin/bash\ncrash -i $(INSTALL_TXT)/$(CRASH_TXT_NAME)" \
		> $(CRASH_ZFS) && chmod +x $(CRASH_ZFS)

uninstall:
	@for f in $(CRASH_SO); do					\
		rm -rfv $(INSTALL_DIR)/$$f;				\
	done
	@rm -rfv $(INSTALL_PYDIR)
	@rm -fv $(CRASH_ZFS)

CFLAGS           += -std=gnu99 -Wall -Werror
CRASH_EXT_CFLAGS += -shared -rdynamic -fPIC $(CFLAGS)
CRASH_EXT_CFLAGS += -D$(CRASH_CPU_TYPE)
CRASH_EXT_CFLAGS += -DCRASH_VERSION=\'$(CRASH_VERSION)\'
CRASH_EXT_CFLAGS += -DCRASH_CPU_TYPE=\'$(CRASH_CPU_TYPE)\'
CRASH_EXT_CFLAGS += -D__COMPILE_EXTPY_SO__
CRASH_EXT_CFLAGS += -I../inc/$(CRASH_VERSION)
CRASH_EXT_CFLAGS += -include crash_version.h

%.so: %.c check_args
	@echo "  CC $@"
	@$(CC) $(CRASH_EXT_CFLAGS) -o $@ $<
