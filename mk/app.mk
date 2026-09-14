SDK_ROOT ?= /opt/SDK-B288
CROSS := $(SDK_ROOT)/usr/bin/arm-obreey-linux-gnueabi-
CC := $(CROSS)gcc
CXX := $(CROSS)g++
SYSROOT := $(SDK_ROOT)/usr/arm-obreey-linux-gnueabi/sysroot
TARGET_FLAGS := -march=armv7-a -mfpu=neon -mfloat-abi=softfp
APP := $(notdir $(CURDIR))
BUILD_DIR := ../../build/$(APP)
SOURCES := $(wildcard src/*.c src/*.cpp) $(EXTRA_SOURCES)
OBJECTS := $(patsubst src/%,$(BUILD_DIR)/%.o,$(SOURCES))
CPPFLAGS += -I$(SYSROOT)/usr/local/include
CFLAGS ?= -O2 -g -Wall -Wextra
CXXFLAGS ?= -O2 -g -Wall -Wextra
LDFLAGS += -L$(SYSROOT)/usr/local/lib \
           -Wl,-rpath-link,$(SYSROOT)/usr/local/lib \
           -Wl,-rpath-link,$(SYSROOT)/usr/lib
LDLIBS += -linkview

.PHONY: all clean check
all: $(BUILD_DIR)/$(APP).app

$(BUILD_DIR)/$(APP).app: $(OBJECTS) $(MAKEFILE_LIST)
	$(if $(filter %.cpp,$(SOURCES)),$(CXX),$(CC)) $(TARGET_FLAGS) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.c.o: src/%.c Makefile ../../mk/app.mk
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.cpp.o: src/%.cpp Makefile ../../mk/app.mk
	@mkdir -p $(@D)
	$(CXX) $(TARGET_FLAGS) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

check: all
	file $(BUILD_DIR)/$(APP).app
	$(CROSS)readelf -h $(BUILD_DIR)/$(APP).app | grep -q 'Machine:.*ARM'
	$(CROSS)readelf -l $(BUILD_DIR)/$(APP).app | grep -q '/lib/ld-linux.so.3'
	$(CROSS)readelf -d $(BUILD_DIR)/$(APP).app | grep -q 'Shared library: \[libinkview.so\]'

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECTS:.o=.d)
