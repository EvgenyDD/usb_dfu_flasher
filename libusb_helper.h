#ifndef LIBUSB_HELPER_H__
#define LIBUSB_HELPER_H__

#include <libusb-1.0/libusb.h>

static inline const char *libusb_err2str(int err)
{
	switch(err)
	{
	case LIBUSB_ERROR_IO: return "Input/output error";
	case LIBUSB_ERROR_INVALID_PARAM: return "Invalid parameter";
	case LIBUSB_ERROR_ACCESS: return "Access denied (insufficient permissions)";
	case LIBUSB_ERROR_NO_DEVICE: return "No such device (it may have been disconnected)";
	case LIBUSB_ERROR_NOT_FOUND: return "Entity not found";
	case LIBUSB_ERROR_BUSY: return "Resource busy";
	case LIBUSB_ERROR_TIMEOUT: return "Timeout";
	case LIBUSB_ERROR_OVERFLOW: return "Overflow";
	case LIBUSB_ERROR_PIPE: return "Pipe error";
	case LIBUSB_ERROR_INTERRUPTED: return "System call interrupted (perhaps due to signal)";
	case LIBUSB_ERROR_NO_MEM: return "Insufficient memory";
	case LIBUSB_ERROR_NOT_SUPPORTED: return "Operation not supported or unimplemented on this platform";
	case LIBUSB_ERROR_OTHER: return "Other error";
	default: return "Unknown error";
	}
}

#endif // LIBUSB_HELPER_H__