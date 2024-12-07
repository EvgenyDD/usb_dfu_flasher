EXE_NAME=usb_dfu_flasher

SOURCES += $(call rwildcard, ., *.c *.S *.s)

ifneq ($(findstring MINGW32,$(uname)),)
TCHAIN = x86_64-w64-mingw32-
# pacman -S mingw-w64-x86_64-libusb
endif

CFLAGS   += -fvisibility=hidden -funsafe-math-optimizations -fdata-sections -ffunction-sections -fno-move-loop-invariants
CFLAGS   += -fmessage-length=0 -fno-exceptions -fno-common -fno-builtin -ffreestanding
CFLAGS   += $(C_FULL_FLAGS)
CFLAGS   += -Werror

EXT_LIBS += usb-1.0 m

include core.mk

TEST_OUTPUT=tests
.PHONY: tests
tests: $(EXECUTABLE)
	@echo "======================================================="
	@-sudo valgrind --tool=memcheck \
		--trace-children=yes \
		--demangle=yes \
		--log-file="${TEST_OUTPUT}.vg.out" \
		--leak-check=full \
		--show-reachable=yes \
		--run-libc-freeres=yes \
		-s \
		$< ${REDIR_OUTPUT}
	@if ! tail -1 "${TEST_OUTPUT}.vg.out" | grep -q "ERROR SUMMARY: 0 errors"; then \
		echo "==== ERROR: valgrind found errors during execution ===="; \
		tail -1 "${TEST_OUTPUT}.vg.out"; \
	else \
		echo "================ No errors found ======================"; \
	fi