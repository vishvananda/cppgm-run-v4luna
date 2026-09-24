DEFAULT_BUILD_JOBS = $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)
ifeq ($(findstring -j,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(DEFAULT_BUILD_JOBS)
endif
ifeq ($(MAKELEVEL),0)
CPPGM_MAKE_JOB_FLAGS := $(filter -j% --jobs=%,$(MAKEFLAGS))
CPPGM_MAKE_JOB_LIMIT := $(patsubst --jobs=%,%,$(patsubst -j%,%,$(lastword $(CPPGM_MAKE_JOB_FLAGS))))
ifneq ($(CPPGM_MAKE_JOB_LIMIT),)
CPPGM_MAKE_LOW_JOB_LIMIT = $(shell \
	limit='$(CPPGM_MAKE_JOB_LIMIT)'; cpus='$(DEFAULT_BUILD_JOBS)'; \
	if [ "$$limit" -gt 0 ] 2>/dev/null && [ "$$cpus" -gt "$$limit" ] 2>/dev/null; then echo 1; else echo 0; fi)
ifeq ($(CPPGM_MAKE_LOW_JOB_LIMIT),1)
$(warning make is limited to -j$(CPPGM_MAKE_JOB_LIMIT) on a $(DEFAULT_BUILD_JOBS)-core machine; large compiler builds, especially self-host and PA34/inception builds, will be very slow. Omit -j or use -j$(DEFAULT_BUILD_JOBS).)
endif
endif
endif

TARGETS = \
	abimangle \
	pptoken \
	posttoken \
	ppexpr \
	preproc \
	lowir \
	lowiropt \
	lowir2native \
	cppgm++

CXX ?= g++
V ?= 0
Q = $(if $(filter 1,$(V)),,@)
quiet = @if [ "$(V)" != "1" ]; then printf '  %-7s %s\n' '$(1)' '$(2)'; fi
CPPGM_TEST_RUNNER ?= 1
CPPGM_STDLIB_FLAGS ?=
CC_FLAGS ?= -std=gnu++11 -Wall -O3 -pthread $(CPPGM_STDLIB_FLAGS)
TEST_RUNNER_SHARED_FLAGS = $(if $(filter 1,$(CPPGM_TEST_RUNNER)),-DTEST_RUNNER_ENABLE,)
TEST_RUNNER_ENTRY_FLAGS = $(if $(filter 1,$(CPPGM_TEST_RUNNER)),-Dmain=test_runner_real_main,)
ENTRY_CC_FLAGS = $(CC_FLAGS) $(TEST_RUNNER_ENTRY_FLAGS)
SRC = src
OBJ ?= ../obj
OBJDIR = $(OBJ)/dev
DEPDIR = $(OBJDIR)/.d
INC = -I$(SRC)

include frontend_source_sets.mk
$(foreach target,$(TARGETS),$(if $(filter undefined,$(origin FRONTEND_OBJ_BASENAMES_$(target))),$(error missing FRONTEND_OBJ_BASENAMES_$(target) in frontend_source_sets.mk),))

frontend_obj_basenames = $(FRONTEND_OBJ_BASENAMES_$(1))
frontend_objs = $(addprefix $(OBJDIR)/,$(addsuffix .o,$(call frontend_obj_basenames,$(1))))
entry_obj = $(OBJDIR)/entry/$(1).o
runner_obj = $(if $(filter 1,$(CPPGM_TEST_RUNNER)),$(OBJDIR)/test_runner_enabled.o)
link_objs = $(call entry_obj,$(1)) $(call frontend_objs,$(1)) $(call runner_obj)
all_obj_basenames = $(sort $(foreach target,$(TARGETS),$(call frontend_obj_basenames,$(target))))
COMPILE_CONFIG_STAMP = $(OBJDIR)/.compile_config
RUNNER_STATE_STAMP = $(OBJDIR)/.test_runner_mode

all: $(TARGETS)

define FRONTEND_RULES
$(1): $(call link_objs,$(1)) $(RUNNER_STATE_STAMP) | $(OBJDIR)
	$(call quiet,LINK,$$@)
	$(Q)$(CXX) $(CC_FLAGS) $(INC) -o $$@.tmp $(call link_objs,$(1))
	@if ! cmp -s $$@.tmp $$@ 2>/dev/null; then mv -f $$@.tmp $$@; else rm -f $$@.tmp; fi

$(call entry_obj,$(1)): $(1).cpp $(COMPILE_CONFIG_STAMP)
	@mkdir -p $$(@D) $(DEPDIR)/entry
	$(call quiet,CXX,$$@)
	$(Q)$(CXX) $(ENTRY_CC_FLAGS) $(INC) -c -MT $$@ -MMD -MP -MF $(DEPDIR)/entry/$(1).Td -o $$@ $$<
	@mv -f $(DEPDIR)/entry/$(1).Td $(DEPDIR)/entry/$(1).d
endef

$(foreach target,$(TARGETS),$(eval $(call FRONTEND_RULES,$(target))))

$(OBJDIR):
	@mkdir -p $@

FORCE:

$(COMPILE_CONFIG_STAMP): FORCE | $(OBJDIR)
	@printf '%s\n%s\n' "$(CC_FLAGS)" "$(ENTRY_CC_FLAGS)" > $@.tmp
	@if ! cmp -s $@.tmp $@ 2>/dev/null; then mv -f $@.tmp $@; else rm -f $@.tmp; fi

$(RUNNER_STATE_STAMP): FORCE | $(OBJDIR)
	@current=$$(cat $@ 2>/dev/null || true); \
	if [ "$$current" != "$(CPPGM_TEST_RUNNER)" ]; then \
		printf '%s\n' '$(CPPGM_TEST_RUNNER)' > $@; \
	fi

$(OBJDIR)/test_runner_enabled.o: $(SRC)/$(FRONTEND_TEST_RUNNER_SOURCE_ID).cpp $(COMPILE_CONFIG_STAMP)
	@mkdir -p $(@D) $(DEPDIR)
	$(call quiet,CXX,$@)
	$(Q)$(CXX) $(CC_FLAGS) $(TEST_RUNNER_SHARED_FLAGS) $(INC) -c -MT $@ -MMD -MP -MF $(DEPDIR)/test_runner_enabled.Td -o $@ $<
	@mv -f $(DEPDIR)/test_runner_enabled.Td $(DEPDIR)/test_runner_enabled.d

$(OBJDIR)/%.o: $(SRC)/%.cpp $(COMPILE_CONFIG_STAMP)
	@mkdir -p $(@D) $(dir $(DEPDIR)/$*)
	$(call quiet,CXX,$@)
	$(Q)$(CXX) $(CC_FLAGS) $(INC) -c -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td -o $@ $<
	@mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

clean:
	-rm -f $(TARGETS)
	-rm -rf $(OBJDIR)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(addprefix $(DEPDIR)/,$(addsuffix .d,$(all_obj_basenames)))
-include $(addprefix $(DEPDIR)/entry/,$(addsuffix .d,$(TARGETS)))
-include $(DEPDIR)/test_runner_enabled.d

.PHONY: all clean FORCE
