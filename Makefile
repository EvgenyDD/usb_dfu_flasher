EXE_NAME=usb_dfu_flasher

TCHAIN = x86_64-w64-mingw32-

SOURCES += $(call rwildcard, ., *.c *.S *.s)

EXT_LIBS += usb-1.0

# pacman -S mingw-w64-x86_64-libusb

include core.mk