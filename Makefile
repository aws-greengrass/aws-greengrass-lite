# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

BUILDDIR ?= build
DESTDIR ?= /usr/local
PROFILE ?= release

MAKEFLAGS += --no-builtin-rules
.DELETE_ON_ERROR:
.SECONDEXPANSION:

.PHONY: all clean install compile_commands.json

all:

clean:
	-rm -r $(BUILDDIR)

compile_commands.json:
	bear -- $(MAKE) -Bk $(COMP_DB_TARGETS)

ifeq (,$(filter clean,$(MAKECMDGOALS)))

CPPFLAGS += -D_FORTIFY_SOURCE=2
CFLAGS += -std=gnu11 -pedantic -Wall -Wextra -Wvla -Wshadow -Wformat=2 \
		-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition \
		-Wunused -Wundef -Wconversion -Wredundant-decls -Wdate-time \
		-Wstack-protector -Wframe-larger-than=512 \
		-fPIE -fvisibility=hidden -fno-semantic-interposition \
		-fstack-protector-strong -fstack-clash-protection
LDFLAGS += -pie -Wl,-z,relro,-z,now,-z,noexecstack -Wl,--as-needed \
		-Wl,--enable-new-dtags,--hash-style=gnu

include profiles/$(PROFILE).mk

CFLAGS += $(EXTRA_CFLAGS)

$(and $(shell mkdir -p $(BUILDDIR)),)

core_deps := $(MAKEFILE_LIST)

# Rebuild if environment changes
ifneq (,$(filter shell-export,$(.FEATURES)))
env := $(shell env)
ifneq ($(file <$(BUILDDIR)/env),$(env))
$(file >$(BUILDDIR)/env,$(env))
endif
core_deps += $(BUILDDIR)/env
endif

$(BUILDDIR)/%.d: %.c $(core_deps)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -MM -MP -MT'$(@:.d=.o) $@' -MF$@ $<

$(BUILDDIR)/%.o: %.c $(core_deps)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

# misc functions for template
include_flags = $(foreach lib,$1,$(addprefix -I$(lib)/,$($(lib)_INCDIRS)))
get_archives = $(foreach lib,$1,$(BUILDDIR)/$(lib)/$(notdir $(lib)).a)
get_lib_closure = $(if $($1_LIBS),\
		$(foreach lib,$($1_LIBS),$(lib) $(call get_lib_closure,$(lib))))

# template for each package directory
define dir_template
ifndef $(notdir $1)_LOADED
$(notdir $1)_LOADED := 1

include $1/ggl.mk

$1_INCDIRS ?= include
$1_SRCDIR ?= src

ifneq ($$(wildcard $1/$$($1_SRCDIR)),)
$1_SRCS := $$(shell find $1/$$($1_SRCDIR) -name '*.c')
endif
$1_OBJS := $$(patsubst %.c,$(BUILDDIR)/%.o,$$($1_SRCS))
$1_DEPS := $$($1_OBJS:.o=.d)

$$($1_OBJS) $$($1_DEPS): $1/ggl.mk
$$($1_OBJS) $$($1_DEPS): CPPFLAGS += $$($1_CPPFLAGS)
$$($1_OBJS) $$($1_DEPS): CPPFLAGS += $$(call include_flags,$1)
$$($1_OBJS) $$($1_DEPS): CPPFLAGS += \
		$$(subst -I,-isystem ,$$(call include_flags,$$($1_LIBS)))
ifdef $1_PKGS
$$($1_OBJS) $$($1_DEPS): CPPFLAGS += \
		$$(subst -I,-isystem ,$$(shell pkg-config --cflags-only-I $$($1_PKGS)))
endif
$$($1_OBJS): CFLAGS += $$($1_CFLAGS)

include $$($1_DEPS)

COMP_DB_TARGETS += $$($1_OBJS)

$(BUILDDIR)/$1/$(notdir $1).a: $$($1_OBJS)
	$$(AR) rcs --thin $$@ $$($1_OBJS)

ifdef $1_BIN
ifdef $1_PKGS
$(BUILDDIR)/bin/$$($1_BIN): LDFLAGS += \
		$$(shell pkg-config --libs-only-L $$($1_PKGS))
$(BUILDDIR)/bin/$$($1_BIN): LDLIBS += \
		$$(shell pkg-config --libs-only-l $$($1_PKGS))
endif # $1_PKGS
$(BUILDDIR)/bin/$$($1_BIN): CFLAGS += $$($1_CFLAGS)
$(BUILDDIR)/bin/$$($1_BIN): LDFLAGS += $$($1_LDFLAGS)
$(BUILDDIR)/bin/$$($1_BIN): LDLIBS += $$($1_LDLIBS)
$(BUILDDIR)/bin/$$($1_BIN): $$($1_OBJS) \
		$$$$(call get_archives,$$$$(call get_lib_closure,$1))
	@mkdir -p $$(@D)
	$$(CC) $$(CFLAGS) $$(LDFLAGS) -o $$@ $$+ $$(LDLIBS)

all: $(BUILDDIR)/bin/$$($1_BIN)

install:: $(BUILDDIR)/bin/$$($1_BIN)
	install -D $(BUILDDIR)/bin/$$($1_BIN) $(DESTDIR)/bin/$$($1_BIN)
endif # ifdef $1_BIN
endif # $(notdir $1)_LOADED
endef # dir_template

DIRS = $(shell find * -maxdepth 1 -type d \
		-exec test -f '{}'/ggl.mk \; -print)

$(foreach dir,$(DIRS),$(eval $(call dir_template,$(dir))))

endif # ifneq ($(MAKECMDGOALS),clean)
