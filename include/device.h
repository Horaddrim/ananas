#include "types.h"

#ifndef __DEVICE_H__
#define __DEVICE_H__

typedef struct DEVICE* device_t;
typedef struct DRIVER* driver_t;
typedef struct PROBE* probe_t;

#define DEVICE_MAX_RESOURCES	16

enum resource_type_t {
	RESTYPE_UNUSED,
	RESTYPE_MEMORY,
	RESTYPE_IO,
	RESTYPE_IRQ
};

struct RESOURCE {
	enum resource_type_t	type;
	unsigned int	base;
	unsigned int	length;
};

/*
 *  This describes a device driver.
 */
struct DRIVER {
	char*   name;

	int	(*drv_probe)(device_t);
	int	(*drv_attach)(device_t);
	ssize_t	(*drv_write)(device_t, const char*, size_t);
	ssize_t	(*drv_read)(device_t, char*, size_t);
};

/*
 * This describes a device; it is generally attached but this structure
 * is also used during probe / attach phase.
 */
struct DEVICE {
	/* Device's driver */
	driver_t	driver;

	/* Parent device */
	device_t	parent;

	/* Unit number */
	unsigned int	unit;

	/* Device resources */
	struct RESOURCE	resource[DEVICE_MAX_RESOURCES];

	/* Formatted name XXX */
	char		name[128 /* XXX */];
};

/*
 * The Device Probe structure describes a possible relationship between a bus
 * and a device (i.e. device X may appear on bus Y). Once a bus is attaching,
 * it will use these declarations as an attempt to add all possible devices on
 * the bus.
 */
struct PROBE {
	/* Driver we are attaching */
	driver_t	driver;

	/* Busses this device appears on */
	const char*	bus[];
};

#define DRIVER_PROBE(drvr) \
	struct PROBE probe_##drvr = { \
		.driver = &drv_##drvr, \
		.bus = {

#define DRIVER_PROBE_BUS(bus) \
			STRINGIFY(bus),

#define DRIVER_PROBE_END() \
			NULL \
		} \
	};

void device_init();
device_t device_alloc(device_t bus, driver_t drv);
int device_attach_single(device_t dev);

int device_get_resources(device_t dev, const char** hints);

#endif /* __DEVICE_H__ */