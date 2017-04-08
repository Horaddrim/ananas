#include <ananas/types.h>
#include <ananas/device.h>
#include <ananas/error.h>
#include <ananas/list.h>
#include <ananas/lib.h>
#include <ananas/thread.h>
#include <ananas/pcpu.h>
#include <ananas/schedule.h>
#include <ananas/trace.h>
#include <ananas/mm.h>
#include <machine/param.h> /* XXX for PAGE_SIZE */
#include "config.h"
#include "usb-bus.h"
#include "usb-core.h"
#include "usb-device.h"
#include "usb-transfer.h"

#if 0
# define DPRINTF kprintf
#else
# define DPRINTF(...)
#endif

TRACE_SETUP;

namespace Ananas {
namespace USB {

static thread_t usbtransfer_thread;
static semaphore_t usbtransfer_sem;
static TransferQueue usbtransfer_complete;
static spinlock_t usbtransfer_lock = SPINLOCK_DEFAULT_INIT;

static Transfer*
CreateControlTransfer(USBDevice& usb_dev, int req, int recipient, int type, int value, int index, void* buf, size_t* len, int write)
{
	/* Make the USB transfer itself */
	int flags = 0;
	if (buf != NULL)
		flags |= TRANSFER_FLAG_DATA;
	if (write)
		flags |= TRANSFER_FLAG_WRITE;
	else
		flags |= TRANSFER_FLAG_READ;
	auto xfer = AllocateTransfer(usb_dev, TRANSFER_TYPE_CONTROL, flags, 0, (len != NULL) ? *len : 0);
	xfer->t_control_req.req_type = TO_REG32((write ? 0 : USB_CONTROL_REQ_DEV2HOST) | USB_CONTROL_REQ_RECIPIENT(recipient) | USB_CONTROL_REQ_TYPE(type));
	xfer->t_control_req.req_request = TO_REG32(req);
	xfer->t_control_req.req_value = TO_REG32(value);
	xfer->t_control_req.req_index = index;
	xfer->t_control_req.req_length = (len != NULL) ? *len : 0;
	xfer->t_length = (len != NULL) ? *len : 0;
	return xfer;
}

errorcode_t
PerformControlTransfer(USBDevice& usb_dev, int req, int recipient, int type, int value, int index, void* buf, size_t* len, int write)
{
	auto xfer = CreateControlTransfer(usb_dev, req, recipient, type, value, index, buf, len, write);

	DPRINTF("usb_control_xfer(): req %d value %x -> xfer=%p\n", req, value, xfer);

	/* Now schedule the transfer and until it's completed XXX Timeout */
	ScheduleTransfer(*xfer);
	sem_wait_and_drain(&xfer->t_semaphore);
	if (xfer->t_flags & TRANSFER_FLAG_ERROR) {
		DPRINTF("usb_control_xfer(): xfer %p ERROR\n", xfer);
		FreeTransfer(*xfer);
		return ANANAS_ERROR(IO);
	}

	if (buf != NULL && len != NULL) {
		/* XXX Ensure we don't return more than the user wants to know */
		if (*len > xfer->t_result_length)
			*len = xfer->t_result_length;
		memcpy(buf, xfer->t_data, *len);
	}

	FreeTransfer(*xfer);
	DPRINTF("usb_control_xfer(): xfer %p done\n", xfer);
	return ananas_success();
}

static inline Device&
usbtransfer_get_hcd_device(Transfer& xfer)
{
	/* All USB_DEVICE's are on usbbusX; the bus' parent is the HCD to use */
	return *xfer.t_device.ud_bus.d_Parent;
}

static errorcode_t
SetupTransfer(Transfer& xfer)
{
	auto& hcd_dev = usbtransfer_get_hcd_device(xfer);
	KASSERT(hcd_dev.GetUSBDeviceOperations() != NULL, "transferring without usb transfer");
	return hcd_dev.GetUSBDeviceOperations()->SetupTransfer(xfer);
}

Transfer*
AllocateTransfer(USBDevice& usb_dev, int type, int flags, int endpt, size_t maxlen)
{
	auto usb_xfer = new Transfer(usb_dev, type, flags);
	DPRINTF("usbtransfer_alloc: xfer=%x type %d\n", usb_xfer, type);
	usb_xfer->t_length = maxlen; /* transfer buffer length */
	usb_xfer->t_address = usb_dev.ud_address;
	usb_xfer->t_endpoint = endpt;
	sem_init(&usb_xfer->t_semaphore, 0);

	errorcode_t err = SetupTransfer(*usb_xfer);
	if (ananas_is_failure(err)) {
		kfree(usb_xfer);
		return NULL;
	}
	return usb_xfer;
}

errorcode_t
ScheduleTransfer(Transfer& xfer)
{
	/* All USB_DEVICE's are on usbbusX; the bus' parent is the HCD to use */
	auto& hcd_dev = usbtransfer_get_hcd_device(xfer);
	auto& usb_dev = xfer.t_device;

	/* Schedule the transfer; we are responsible for locking here */
	usb_dev.Lock();
	errorcode_t err = hcd_dev.GetUSBDeviceOperations()->ScheduleTransfer(xfer);
	usb_dev.Unlock();
	return err;
}

void
FreeTransfer_Locked(Transfer& xfer)
{
	auto& usb_dev = xfer.t_device;
	auto& hcd_dev = usbtransfer_get_hcd_device(xfer);
	usb_dev.AssertLocked();

	hcd_dev.GetUSBDeviceOperations()->CancelTransfer(xfer);
	hcd_dev.GetUSBDeviceOperations()->TearDownTransfer(xfer);
	delete &xfer;
}

void
FreeTransfer(Transfer& xfer)
{
	auto& usb_dev = xfer.t_device;

	DPRINTF("usbtransfer_free: xfer=%x type %d\n", xfer, xfer->t_type);
	usb_dev.Lock();
	FreeTransfer_Locked(xfer);
	usb_dev.Unlock();
}

void
CompleteTransfer_Locked(Transfer& xfer)
{
	auto& usb_dev = xfer.t_device;
	usb_dev.AssertLocked();
	KASSERT(xfer.t_flags & TRANSFER_FLAG_PENDING, "completing transfer that isn't pending %p", xfer.t_hcd);

	/* Transfer is complete, so we can remove the pending flag */
	xfer.t_flags &= ~TRANSFER_FLAG_PENDING;
	LIST_REMOVE_IP(&xfer.t_device.ud_transfers, pending, &xfer);

	/*
	 * This is generally called from interrupt context, so schedule a worker to
	 * process the transfer; if the transfer doesn't have a callback function,
	 * assume we'll just have to signal its semaphore.
	 */
	if (xfer.t_callback != nullptr) {
		spinlock_lock(&usbtransfer_lock);
		LIST_APPEND_IP(&usbtransfer_complete, completed, &xfer);
		spinlock_unlock(&usbtransfer_lock);

		sem_signal(&usbtransfer_sem);
	} else {
		sem_signal(&xfer.t_semaphore);
	}
}

void
CompleteTransfer(Transfer& xfer)
{
	auto& usb_dev = xfer.t_device;

	usb_dev.Lock();
	CompleteTransfer_Locked(xfer);
	usb_dev.Unlock();
}

static void
transfer_thread(void* arg)
{
	while(1) {
		/* Wait until there's something to report */
		sem_wait(&usbtransfer_sem);

		/* Fetch an entry from the queue */
		while (1) {
			spinlock_lock(&usbtransfer_lock);
			if(LIST_EMPTY(&usbtransfer_complete)) {
				spinlock_unlock(&usbtransfer_lock);
				break;
			}
			Transfer* xfer = LIST_HEAD(&usbtransfer_complete);
			LIST_POP_HEAD_IP(&usbtransfer_complete, completed);
			spinlock_unlock(&usbtransfer_lock);

			/* And handle it */
			KASSERT(xfer->t_callback != nullptr, "xfer %p in completed list without callback?", xfer);
			xfer->t_callback(*xfer);

			/*
			 * At this point, xfer may be freed, resubmitted or simply left lingering for
			 * the caller - we can't touch it at this point.
			 */
		}
	}
}

void
usbtransfer_init()
{
	LIST_INIT(&usbtransfer_complete);
	sem_init(&usbtransfer_sem, 0);

	/* Create a kernel thread to handle USB completed messages */
	kthread_init(&usbtransfer_thread, "usb-transfer", &transfer_thread, NULL);
	thread_resume(&usbtransfer_thread);
}

} // namespace USB
} // namespace Ananas

/* vim:set ts=2 sw=2: */