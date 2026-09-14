APP := $(notdir $(CURDIR))
BUILD_DIR := ../../build/$(APP)
.PHONY: all check clean
all:
	cmake -S . -B $(BUILD_DIR)/qt6 -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR)/qt6 -j2
check: all
	python3 ../../tools/check_qt6_app.py $(BUILD_DIR)/$(APP).app
clean:
	@if [ -f $(BUILD_DIR)/qt6/CMakeCache.txt ]; then cmake --build $(BUILD_DIR)/qt6 --target clean; fi
