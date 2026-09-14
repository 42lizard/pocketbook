APPS := $(sort $(notdir $(patsubst %/Makefile,%,$(wildcard apps/*/Makefile))))

ifdef APP
ifneq ($(APP),$(filter $(APP),$(APPS)))
$(error Unknown app '$(APP)'. Available apps: $(APPS))
endif
APPS := $(APP)
endif

.PHONY: all check clean list $(addprefix app-,$(APPS))
all: $(addprefix app-,$(APPS))

$(addprefix app-,$(APPS)):
	$(MAKE) -C apps/$(patsubst app-%,%,$@)

check clean:
	@set -e; for app in $(APPS); do $(MAKE) -C apps/$$app $@; done

list:
	@printf '%s\n' $(APPS)
