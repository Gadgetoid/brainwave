# Builds the CE app, the firmware and the host tests.

.PHONY: all ce firmware partitions push test clean

all: ce firmware

ce:
	@test -n "$(JORNADA_TOOLCHAIN)" || { echo "Set JORNADA_TOOLCHAIN to the jornada-72x-tools directory"; exit 1; }
	cmake -S ce -B build/ce -G Ninja -DCMAKE_TOOLCHAIN_FILE=$(JORNADA_TOOLCHAIN)/cmake/jornada-ce.cmake -DJORNADA_TOOLCHAIN=$(JORNADA_TOOLCHAIN)
	cmake --build build/ce

firmware:
	cmake -S firmware -B build/firmware -G Ninja
	cmake --build build/firmware
	@echo build/firmware/brainwave.uf2

partitions:
	mkdir -p build/firmware
	picotool partition create firmware/partitions.json build/firmware/partitions.uf2
	@echo build/firmware/partitions.uf2

push:
	python3 tools/push-firmware.py $(MODULE) build/firmware/brainwave.bin

test:
	mkdir -p build/tests
	c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -Ifirmware/src tests/napt_test.cpp firmware/src/napt.cpp -o build/tests/napt_test
	build/tests/napt_test

clean:
	rm -rf build
