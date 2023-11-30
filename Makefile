.PHONY: all clean clean_crash_headers do_configure check_configure

TARGET        =
TARGET_CFLAGS =
CRASH_VERSION =
CONFIGURED    = 0

ifneq ($(ACTION),configure)
CONFIGURE_LINES = $(shell make do_configure	\
    ACTION=configure				\
    ORIG_MAKECMDGOALS=$(MAKECMDGOALS)		\
    target=$(target)				\
    target_cflags=$(target_cflags)		\
    crash_version=$(crash_version))
$(foreach line,$(CONFIGURE_LINES),$(eval $(line)))
endif

DIR_BUILD   = build
DIR_RELEASE = release

BINS += extpy.so
extpy-objs += src/extpy.crash.o

BINS += runso.so
runso-objs += src/runso.crash.o

CFLAGS += -rdynamic -fPIC -std=gnu99 -Wall -Werror

all: check_configure $(patsubst %.so,$(DIR_RELEASE)/%.so,$(BINS))

clean:
	@$(RM) -rv $(DIR_BUILD) $(DIR_RELEASE)

do_configure:
	@configure=$$(mktemp);						\
		$(CC) $(CFLAGS) src/configure.c -o $$configure;		\
		$$configure; rc=$$?;					\
		rm -rf $$configure;					\
		[ $$rc -eq 0 ]

check_configure:
	@[ $(CONFIGURED) -eq 1 ]

CRASH_EXT_CFLAGS += -D$(TARGET) $(TARGET_CFLAGS)
CRASH_EXT_CFLAGS += -DCRASH_VERSION=\'$(CRASH_VERSION)\'
CRASH_EXT_CFLAGS += -DCRASH_CPU_TYPE=\'$(TARGET)\'
CRASH_EXT_CFLAGS += -D__COMPILE_EXTPY_SO__
CRASH_EXT_CFLAGS += -Iinc/crash
CRASH_EXT_CFLAGS += -include crash_version.h
CRASH_EXT_CFLAGS += -Iinc/crash/$(CRASH_VERSION)

$(DIR_BUILD)/%.crash.o: %.c
	@mkdir -p $$(dirname $@)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) $(CRASH_EXT_CFLAGS) -c -o $@ $<

clean_crash_headers:
	@rm -rfv inc/crash/*/

define BUILD_CRASH_SO
$$(DIR_RELEASE)/$(1).so: $$(patsubst %.o,$$(DIR_BUILD)/%.o,$$($(1)-objs))
	@mkdir -p $$$$(dirname $$@)
	@echo "  LD $$@"
	@$$(CC) -shared $(CFLAGS) $(CRASH_EXT_CFLAGS) $$^ -o $$@
endef

$(foreach bin,$(patsubst %.so,%,$(BINS)),$(eval $(call BUILD_CRASH_SO,$(bin))))
