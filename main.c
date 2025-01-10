
#include "libusb_helper.h"
#include "percent_tracker.h"
#include "timedate.h"
#include <ctype.h>
#include <libusb-1.0/libusb.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_FLASHER_VER "2.0.0"

#define REBOOT_TO 400
#define RETRY_CNT 5
#define QUANT_FLASH 256

extern int parse_file_cfg(const char *file_name);
extern int parse_file_fw(const char *file_name);

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

typedef enum
{
	FW_PREBOOT = 0,
	FW_BOOT,
	FW_APP,
} FW_TYPE_t;

static const char *fw_type_str[] = {"PREBOOT", "BOOT", "APP", "CFG"};

enum
{
	ERR_ARGC = 1,
	ERR_FILE,
	ERR_FILE_READ,
	ERR_REBOOT,
	ERR_WR,
	ERR_RD,
	ERR_CHK,
};

static FILE *f = NULL;
static uint8_t *content = NULL;
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

static int _strncmp_lwr(const char *s0, const char *s1, size_t c)
{
	for(size_t i = 0; i < c; i++)
	{
		if(!s0[i] || !s1[i]) return 1;
		if(tolower(s0[i]) != tolower(s1[i])) return 1;
	}
	return 0;
}

#define EP_REQ_IN LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE
#define EP_REQ_OUT LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE

// Note: wIndex will always be 0 in libusb_control_transfer with WinUSB device

static int dfu_reboot(bool sub_reboot) { return libusb_control_transfer(handle, EP_REQ_OUT, DFU_DETACH, sub_reboot, 0, NULL, 0, 500); }
static int dfu_write(uint8_t fw_index, uint8_t *pkt, uint16_t pkt_len) { return libusb_control_transfer(handle, EP_REQ_OUT, DFU_DNLOAD, fw_index, 0, pkt, pkt_len, 4500); }
static int dfu_get_fw_sts(uint8_t sts[3]) { return libusb_control_transfer(handle, EP_REQ_IN, DFU_GETSTATUS, 0, 0, sts, 3, 500); }
static int dfu_get_fw_type(uint8_t type[1]) { return libusb_control_transfer(handle, EP_REQ_IN, DFU_GETSTATE, 0, 0, type, 1, 500); }
static int dfu_halt(void) { return libusb_control_transfer(handle, EP_REQ_OUT, DFU_CLRSTATUS, 0, 0, NULL, 0, 500); }
static int dfu_halt_specific(uint8_t fw_index, char *app) { return libusb_control_transfer(handle, EP_REQ_OUT, DFU_CLRSTATUS, fw_index, 0, (uint8_t *)app, (uint16_t)strlen(app), 500); }

static int dfu_read(uint8_t fw_index, uint32_t offset, uint8_t *pkt, uint32_t pkt_len)
{
	uint8_t buf[8];
	memcpy(&buf[0], &offset, 4);
	memcpy(&buf[4], &pkt_len, 4);
	int sts = libusb_control_transfer(handle, EP_REQ_OUT, DFU_UPLOAD, fw_index, 0, buf, sizeof(buf), 500);
	if(sts < 0) return sts;
	return libusb_control_transfer(handle, EP_REQ_IN, DFU_UPLOAD, fw_index, 0, pkt, (uint16_t)pkt_len, 500);
}

static int find_usb_device(bool writing, const char *name, char *sub_name, FW_TYPE_t fw_sel)
{
	for(ssize_t i = 0; i < cnt; i++)
	{
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;
		int sts = libusb_get_device_descriptor(dev, &desc);
		if(sts < 0) continue;
		sts = libusb_open(dev, &handle);
		if(sts < 0) continue;

		char buf[256] = {0};
		sts = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, (uint8_t *)buf, sizeof(buf));
		if(sts < 0)
		{
			handle_close();
			continue;
		}

		size_t name_sz = strlen(name);
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
		// char tgt_names[2][256] = {0};
		// strcpy(tgt_names[0], name);
		// strcpy(tgt_names[1], name);
		// strcat(tgt_names[0], "_app");
		// strcat(tgt_names[1], "_ldr");
		// int cmp_app = _strncmp_lwr(tgt_names[0], buf, strlen(tgt_names[0]));
		// int cmp_ldr = _strncmp_lwr(tgt_names[1], buf, strlen(tgt_names[1]));
		// if(cmp_app && cmp_ldr)
		// {
		// 	handle_close();
		// 	continue;
		// }
		fprintf(stderr, "info:\tfound device %x::%x::%s\n", desc.idVendor, desc.idProduct, buf);

		sts = sub_name ? dfu_halt_specific(fw_sel, sub_name) : dfu_halt();
		if(sts < 0)
		{
			fprintf(stderr, "error:\tfailed to halt: %s\n", libusb_err2str(sts));
			return -2;
		}

		if(writing)
		{
			uint8_t fw_type = 0;
			sts = dfu_get_fw_type(&fw_type);
			if(sts < 0)
			{
				fprintf(stderr, "error:\tfailed to get fw type: %d\n", sts);
				return -3;
			}
			if(fw_sel == FW_APP && fw_type == FW_APP)
			{
				fprintf(stderr, "info:\trebooting%sto boot...\n", sub_name ? " sub " : " ");
				sts = dfu_reboot(sub_name != NULL);
				if(sts < 0)
				{
					fprintf(stderr, "error:\tfailed to reboot%sto boot: %d\n", sub_name ? " sub " : " ", sts);
					return -3;
				}
				handle_close();
				delay_ms(REBOOT_TO);
				return 1; // call again
			}
			else if(fw_sel == FW_BOOT && fw_type == FW_BOOT)
			{
				fprintf(stderr, "info:\trebooting%sto app...\n", sub_name ? " sub " : " ");
				sts = dfu_reboot(sub_name != NULL);
				if(sts < 0)
				{
					fprintf(stderr, "error:\tfailed to reboot%sto app: %d\n", sub_name ? " sub " : " ", sts);
					return -3;
				}
				handle_close();
				delay_ms(REBOOT_TO);
				return 1; // call again
			}
			return 0;
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
	if(f) fclose(f);
	if(content) free(content);
	f = NULL;
	list = NULL;
	content = NULL;
}

static size_t file_len(void)
{
	fseek(f, 0, SEEK_END);
	size_t s = (size_t)ftell(f);
	rewind(f);
	return s;
}

static struct
{
	bool write;
	FW_TYPE_t sel;
	char *file_name;
	char *dev_name;
	char *sub_name;
	uint32_t chunk;
} cfg = {0};

static int parse_arg(char *argv[], int argc)
{
	if(argc != 5 && argc != 6 && argc != 7)
	{
		fprintf(stderr, "Error! USB FLASHER [ver. %s]: Wrong argument count!\nUsage:\n\t"
						"  w/r                  - write/read operation\n\t"
						"  p/b/a/c              - fw select: preboot/boot/app/config\n\t"
						"  file                 - firmware binary\n\t"
						"  name                 - device name\n\t"
						"  [optional]  sub name - remote flash device name\n"
						"  [optional+] chunk    - chunk size\n",
				USB_FLASHER_VER);
		return ERR_ARGC;
	}

	int w = strcmp(argv[1], "w") == 0 ? 1 : (strcmp(argv[1], "r") == 0 ? 0 : -1);
	if(w < 0)
	{
		fprintf(stderr, "Error! 1st argument is [r/w], not [%s]!\n", argv[1]);
		return ERR_ARGC;
	}
	cfg.write = w;

	int s = strcmp(argv[2], "p") == 0 ? FW_PREBOOT : (strcmp(argv[2], "b") == 0 ? FW_BOOT : (strcmp(argv[2], "a") == 0 ? FW_APP : (strcmp(argv[2], "c") == 0 ? FW_APP + 1 : -1)));
	if(s < 0)
	{
		fprintf(stderr, "Error! 2st argument is [p/b/a/c], not [%s]!\n", argv[2]);
		return ERR_ARGC;
	}
	if(s == FW_PREBOOT && cfg.write)
	{
		fprintf(stderr, "Error! PREBOOT write is not yet supported!\n");
		return ERR_ARGC;
	}
	cfg.sel = s;

	cfg.file_name = argv[3];
	cfg.dev_name = argv[4];
	cfg.sub_name = argc >= 6 ? argv[5] : NULL;
	cfg.chunk = argc == 7 ? (uint32_t)atoi(argv[6]) : QUANT_FLASH;
	if(cfg.chunk < 1)
	{
		fprintf(stderr, "Error! Chunk size can't be 0!\n");
		return ERR_ARGC;
	}
	if(cfg.chunk > QUANT_FLASH)
	{
		fprintf(stderr, "Error! Chunk size can't be more than QUANT_FLASH!\n");
		return ERR_ARGC;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int sts = parse_arg(argv, argc);
	if(sts) return sts;

	atexit(on_exit_cb);

	sts = libusb_init(NULL);
	if(sts < 0) fprintf(stderr, "error:\tfailed to initialize libusb: %s\n", libusb_err2str(sts));

	libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, 0);

	if(cfg.write)
	{
		f = fopen(cfg.file_name, "rb");
		if(!f)
		{
			fprintf(stderr, "error:\topen file %s\n", cfg.file_name);
			return ERR_FILE;
		}

		size_t content_length = file_len();
		content = malloc(content_length);

		size_t read = fread(content, 1, content_length, f);
		if(read != content_length)
		{
			fprintf(stderr, "error:\tread file (%zu %zu)\n", read, content_length);
			return ERR_FILE_READ;
		}

		fprintf(stderr, "info:\tflashing %s %s to \"%s%s%s\" (%zu bytes)...\n",
				cfg.file_name, fw_type_str[cfg.sel], cfg.dev_name, cfg.sub_name ? ":" : "", cfg.sub_name ? cfg.sub_name : "", content_length);

		cnt = libusb_get_device_list(NULL, &list);
		if(cnt < 0) fprintf(stderr, "error\tlibusb: failed to get device list\n");

		// fprintf(stderr, "info:\tfound %lld USB devices\n", cnt);

		sts = find_usb_device(cfg.write, cfg.dev_name, cfg.sub_name, cfg.sel);
		if(sts == 1)
		{
			handle_close();
#define RETRY 7
			for(uint32_t i = 0; i < RETRY; i++)
			{
				if(list) libusb_free_device_list(list, 1);

				cnt = libusb_get_device_list(NULL, &list);
				if(cnt < 0) fprintf(stderr, "error\tlibusb: failed to get device list\n");

				sts = find_usb_device(cfg.write, cfg.dev_name, cfg.sub_name, cfg.sel);
				if(sts != 0 && i == RETRY - 1)
				{
					fprintf(stderr, "error:\tfailed to reboot device \"%s%s%s\" 2nd time\n", cfg.dev_name, cfg.sub_name ? ":" : "", cfg.sub_name ? cfg.sub_name : "");
					return ERR_REBOOT;
				}
				if(!sts) break;
				delay_ms(200);
			}
		}
		else if(sts != 0)
		{
			fprintf(stderr, "error:\tfailed to find device \"%s%s%s\"\n", cfg.dev_name, cfg.sub_name ? ":" : "", cfg.sub_name ? cfg.sub_name : "");
			return ERR_REBOOT;
		}

		int errc = 1;

		for(uint32_t retry = 0; retry < RETRY_CNT; retry++)
		{
			PERCENT_TRACKER_INIT(tr);
			for(uint32_t off = 0;; off += cfg.chunk)
			{
				if(off >= content_length)
				{
					errc = 0;
					break;
				}
				uint8_t pkt[4 + QUANT_FLASH];
				memcpy(&pkt[0], &off, 4);
				uint32_t size_to_write = (uint32_t)content_length - off > cfg.chunk ? cfg.chunk : (uint32_t)content_length - off;
				memcpy(&pkt[4], &content[off], size_to_write);

				int sts_dfu_write;
				for(uint32_t retr_write = 0; retr_write < RETRY_CNT; retr_write++)
				{
					if((sts_dfu_write = dfu_write(cfg.sel, pkt, 4 + (uint16_t)size_to_write)) >= 0) break;
				}
				if(sts_dfu_write < 0)
				{
					fprintf(stderr, "error:\tfailed to write (%s) @%d\n", libusb_err2str(sts), off);
					errc = ERR_WR;
					break;
				}

				PERCENT_TRACKER_TRACK(tr, (double)off / (double)(content_length),
									  { fprintf(stderr, "\rinfo:\t%.1f%% | pass: %lld sec | est: %lld sec\t\t",
												100.0 * tr.progress,
												tr.time_ms_pass / 1000, tr.time_ms_est / 1000); });
				fflush(stdout);
			}
			if(errc == 0) break;
			if(retry != RETRY_CNT - 1) fprintf(stderr, "error:\ttrying again...\n");
		}
		fprintf(stderr, "\n");

		if(!errc && cfg.sel <= FW_APP)
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

		if(errc == 0)
		{
			sts = dfu_reboot(cfg.sub_name != NULL);
			if(sts < 0)
			{
				fprintf(stderr, "error:\tfailed to reboot: %s\n", libusb_err2str(sts));
				errc = ERR_REBOOT;
			}
		}

		return errc;
	}
	else // read
	{
		f = fopen(cfg.file_name, "wb");
		if(!f)
		{
			fprintf(stderr, "error:\topen file %s\n", cfg.file_name);
			return ERR_FILE;
		}
		fprintf(stderr, "info:\treading \"%s%s%s\" %s to %s...\n", cfg.dev_name, cfg.sub_name ? ":" : "", cfg.sub_name ? cfg.sub_name : "", fw_type_str[cfg.sel], cfg.file_name);

		cnt = libusb_get_device_list(NULL, &list);
		if(cnt < 0) fprintf(stderr, "error\tlibusb: failed to get device list\n");
		sts = find_usb_device(cfg.write, cfg.dev_name, cfg.sub_name, cfg.sel);
		if(sts)
		{
			fprintf(stderr, "error:\tfailed to find device \"%s\"\n", cfg.dev_name);
			return ERR_REBOOT;
		};

		int errc = 1;
		uint8_t pkt[QUANT_FLASH];
		for(uint32_t offset = 0, readed_length = 0;; offset += (uint32_t)sts)
		{
			errc = 1;
			for(uint32_t try = 0; try < 5; try++)
			{
				sts = dfu_read(cfg.sel, offset, pkt, QUANT_FLASH);
				if(sts < 0)
				{
					fprintf(stderr, "\rerror:\tfailed to read (%d) @%d\n", sts, offset);
				}
				else
				{
					errc = 0;
					break;
				}
			}
			if(errc) break;

			readed_length += (uint32_t)sts;
			fprintf(stderr, "\rreading... %d bytes", readed_length);
			if(sts == 0) // done
			{
				fprintf(stderr, "\n");
				if(readed_length == 0) fprintf(stderr, "FW region is invalid (size is 0)\n");
				errc == 0;
				fclose(f);
				f = NULL;
				if(cfg.sel == FW_APP + 1 && readed_length) parse_file_cfg(cfg.file_name);
				if(cfg.sel <= FW_APP && readed_length) parse_file_fw(cfg.file_name);
				break;
			}

			size_t wr_cnt = fwrite(pkt, 1, (size_t)sts, f);
			if(wr_cnt != (size_t)sts)
			{
				fprintf(stderr, "error:\tfailed to write to file %s\n", cfg.file_name);
				errc = 1;
				break;
			}
		}
		fprintf(stderr, errc ? "Error!\n" : "info:\tOK, exiting...\n");
		return errc;
	}
}
