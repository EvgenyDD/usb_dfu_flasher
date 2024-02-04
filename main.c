#include "libusb_helper.h"
#include "percent_tracker.h"
#include "timedate.h"
#include <libusb-1.0/libusb.h>
#include <stdbool.h>
#include <stdio.h>

#define USB_VID_STM 0x0483 // STMicroelectronics
#define REBOOT_TO 400
#define RETRY_CNT 3
#define QUANT_FLASH 256

enum
{
	DFU_DETACH = 0,
	DFU_DNLOAD,
	DFU_UPLOAD,
	DFU_GETSTATUS,
	DFU_CLRSTATUS,
	DFU_GETSTATE,
	DFU_ABORT
};

enum
{
	ERR_ARGC = 1,
	ERR_FILE,
	ERR_FILE_READ,
	ERR_REBOOT,
	ERR_WR,
	ERR_CHK,
};

static progress_tracker_t tr;
static libusb_device **list = NULL;
static ssize_t cnt;
static libusb_device_handle *handle = NULL;

static inline void handle_close(void)
{
	if(handle)
	{
		libusb_close(handle);
		handle = NULL;
	}
}

static int _strncmp_lwr(const char *s0, const char *s1, int c)
{
	for(uint32_t i = 0; i < c; i++)
	{
		if(!s0[i] || !s1[i]) return 1;
		if(tolower(s0[i]) != tolower(s1[i])) return 1;
	}
	return 0;
}

#define EP_REQ_IN LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE
#define EP_REQ_OUT LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE

static int dfu_reboot(void) { return libusb_control_transfer(handle, EP_REQ_OUT, DFU_DETACH, 0, 0, NULL, 0, 500); }
static int dfu_halt(void) { return libusb_control_transfer(handle, EP_REQ_OUT, DFU_CLRSTATUS, 0, 0, NULL, 0, 500); }
static int dfu_write(uint8_t *pkt, uint32_t pkt_len) { return libusb_control_transfer(handle, EP_REQ_OUT, DFU_DNLOAD, 0, 0, pkt, pkt_len, 4500); }
static int dfu_get_fw_sts(uint8_t sts[3]) { return libusb_control_transfer(handle, EP_REQ_IN, DFU_GETSTATUS, 0, 0, sts, 3, 500); }

static int find_usb_device(const char *name, bool is_bootloader)
{
	for(ssize_t i = 0; i < cnt; i++)
	{
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;
		int sts = libusb_get_device_descriptor(dev, &desc);
		if(sts < 0) continue;
		if(desc.idVendor != USB_VID_STM) continue;
		sts = libusb_open(dev, &handle);
		if(sts < 0) continue;

		char buf[256] = {0};
		sts = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, (uint8_t *)buf, sizeof(buf));
		if(sts < 0)
		{
			handle_close();
			continue;
		}

		int name_sz = strlen(name);
		if(strlen(buf) < name_sz)
		{
			handle_close();
			continue;
		}
		if(_strncmp_lwr(name, buf, name_sz) != 0)
		{
			handle_close();
			continue;
		}
		char tgt_names[2][256] = {0};
		strcpy(tgt_names[0], name);
		strcpy(tgt_names[1], name);
		strcat(tgt_names[0], "_app");
		strcat(tgt_names[1], "_ldr");
		int cmp_app = _strncmp_lwr(tgt_names[0], buf, strlen(tgt_names[0]));
		int cmp_ldr = _strncmp_lwr(tgt_names[1], buf, strlen(tgt_names[1]));
		if(cmp_app && cmp_ldr)
		{
			handle_close();
			continue;
		}
		fprintf(stderr, "info:\tfound device %x::%x::%s\n", desc.idVendor, desc.idProduct, buf);
		if(is_bootloader && !cmp_ldr)
		{
			fprintf(stderr, "info:\trebooting to app...\n");
			sts = dfu_reboot();
			if(sts < 0)
			{
				fprintf(stderr, "error:\tfailed to reboot: %s\n", libusb_err2str(sts));
				return -2;
			}
			handle_close();
			delay_ms(REBOOT_TO);
			return 1;
		}
		else if(!is_bootloader && !cmp_app)
		{
			fprintf(stderr, "info:\trebooting to ldr...\n");
			sts = dfu_reboot();
			if(sts < 0)
			{
				fprintf(stderr, "error:\tfailed to reboot: %s\n", libusb_err2str(sts));
				return -2;
			}
			handle_close();
			delay_ms(REBOOT_TO);
			return 1;
		}
		sts = dfu_halt();
		if(sts < 0)
		{
			fprintf(stderr, "error:\tfailed to halt: %s\n", libusb_err2str(sts));
			return -2;
		}
		return 0;
	}
	return -1;
}

static void on_exit_cb(void)
{
	handle_close();
	if(list) libusb_free_device_list(list, 1);
	libusb_exit(NULL);
}

static size_t file_len(FILE *f)
{
	fseek(f, 0, SEEK_END);
	size_t s = (size_t)ftell(f);
	rewind(f);
	return s;
}

int main(int argc, char *argv[])
{
	atexit(on_exit_cb);

	if(argc != 3 && argc != 4)
	{
		fprintf(stderr, "Error! USB FLASHER [ver.1.0.0]: Wrong argument count!\nUsage:\n\t"
						"  file              - firmware binary\n\t"
						"  name              - device name\n\t"
						"  [optional] loader - flash bootloader instead of main firmware\n");
		return ERR_ARGC;
	}

	FILE *f = fopen(argv[1], "rb");
	if(!f)
	{
		fprintf(stderr, "error:\topen file\n");
		return ERR_FILE;
	}

	bool flash_loader = argc == 4;

	size_t content_length = file_len(f);
	uint8_t *content = malloc(content_length);

	int read = fread(content, 1, content_length, f);
	if(read != (int)content_length)
	{
		fprintf(stderr, "error:\tread file (%d %lld)\n", read, content_length);
		return ERR_FILE_READ;
	}

	fprintf(stderr, "info:\tflashing %s%s to \"%s\" (%lld bytes)...\n", argv[1], flash_loader ? " (bootloader)" : "", argv[2], content_length);

	int sts = libusb_init(NULL);
	if(sts < 0) fprintf(stderr, "error:\tfailed to initialize libusb: %s\n", libusb_err2str(sts));

	libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, 3);

	cnt = libusb_get_device_list(NULL, &list);
	if(cnt < 0) fprintf(stderr, "error\tlibusb: failed to get device list\n");

	// fprintf(stderr, "info:\tfound %lld USB devices\n", cnt);

	sts = find_usb_device(argv[2], flash_loader);
	if(sts == 1)
	{
		handle_close();
#define RETRY 7
		for(uint32_t i = 0; i < RETRY; i++)
		{
			if(list) libusb_free_device_list(list, 1);

			cnt = libusb_get_device_list(NULL, &list);
			if(cnt < 0) fprintf(stderr, "error\tlibusb: failed to get device list\n");

			sts = find_usb_device(argv[2], flash_loader);
			if(sts != 0 && i == RETRY - 1)
			{
				fprintf(stderr, "error:\tfailed to reboot device \"%s\" 2nd time\n", argv[2]);
				return ERR_REBOOT;
			}
			if(!sts) break;
			delay_ms(200);
		}
	}
	else if(sts != 0)
	{
		fprintf(stderr, "error:\tfailed to find device \"%s\"\n", argv[2]);
		return ERR_REBOOT;
	}

	int errc = 1;

	for(uint32_t retry = 0; retry < RETRY_CNT; retry++)
	{
		PERCENT_TRACKER_INIT(tr);
		for(uint32_t off = 0;; off += QUANT_FLASH)
		{
			if(off >= content_length)
			{
				errc = 0;
				break;
			}
			uint8_t pkt[4 + QUANT_FLASH];
			memcpy(&pkt[0], &off, 4);
			uint32_t size_to_write = content_length - off > QUANT_FLASH ? QUANT_FLASH : content_length - off;
			memcpy(&pkt[4], &content[off], size_to_write);

			sts = dfu_write(pkt, 4 + size_to_write);
			if(sts < 0)
			{
				fprintf(stderr, "error:\tfailed to write (%s) @%d\n", libusb_err2str(sts), off);
				errc = ERR_WR;
				break;
			}

			PERCENT_TRACKER_TRACK(tr, (double)off / (double)(content_length),
								  { fprintf(stderr, "\rinfo:\t%.1f%% | pass: %ld sec | est: %ld sec\t\t",
											100.0 * tr.progress,
											tr.time_ms_pass / 1000, tr.time_ms_est / 1000); });
			fflush(stdout);
		}
		if(errc == 0) break;
		if(retry != RETRY_CNT - 1) fprintf(stderr, "error:\ttrying again...\n");
	}
	fprintf(stderr, "\n");

	if(!errc)
	{
		uint8_t fw_sts[3];
		sts = dfu_get_fw_sts(fw_sts);
		if(sts < 0) fprintf(stderr, "error:\tfailed to get fw sts: %s\n", libusb_err2str(sts));
		if(fw_sts[0] || fw_sts[1] || fw_sts[2])
		{
			fprintf(stderr, "error:\tfailed to check HW (%d %d %d)\n", fw_sts[0], fw_sts[1], fw_sts[2]);
			errc = ERR_CHK;
		}
	}

	fprintf(stderr, errc ? "error:\tupdate failed\n" : "info:\tOK, exiting...\n");

	// if(!flash_loader)
	{
		sts = dfu_reboot();
		if(sts < 0)
		{
			fprintf(stderr, "error:\tfailed to reboot: %s\n", libusb_err2str(sts));
			errc = ERR_REBOOT;
		}
	}

	return errc;
}
