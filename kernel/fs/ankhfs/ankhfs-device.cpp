#include <ananas/types.h>
#include <ananas/error.h>
#include <ananas/vfs.h>
#include <ananas/vfs/core.h>
#include <ananas/vfs/generic.h>
#include <ananas/driver.h>
#include <ananas/device.h>
#include <ananas/vmspace.h>
#include <ananas/trace.h>
#include <ananas/lib.h>
#include "device.h"
#include "support.h"

TRACE_SETUP;

namespace Ananas {
namespace DeviceManager {
namespace internal {
extern spinlock_t spl_devicequeue;
extern Ananas::DeviceList deviceList;
} // namespace internal
} // namespace DeviceManager

namespace DriverManager {
namespace internal {
Ananas::DriverList& GetDriverList();
} // namespace internal
} // namespace DriverManager

namespace AnkhFS {

namespace {

errorcode_t
HandleReadDir_Device(struct VFS_FILE* file, void* dirents, size_t* len)
{
	struct FetchEntry : IReadDirCallback {
		bool FetchNextEntry(char* entry, size_t maxLength, ino_t& inum) override {
			// XXX we should lock the device here

			// Skip anything that isn't a char/block device - we can do nothing with those
			while (currentDevice != nullptr && currentDevice->GetCharDeviceOperations() == nullptr && currentDevice->GetBIODeviceOperations() == nullptr)
				currentDevice = LIST_NEXT_IP(currentDevice, all);
			if (currentDevice == nullptr)
				return false;

			snprintf(entry, maxLength, "%s%d", currentDevice->d_Name, currentDevice->d_Unit);
			inum = make_inum(SS_Device, makedev(currentDevice->d_Major, currentDevice->d_Unit), 0);
			currentDevice = LIST_NEXT_IP(currentDevice, all);
			return true;
		}

		Ananas::Device* currentDevice = LIST_HEAD(&DeviceManager::internal::deviceList);
	};

	// Fill the root directory with one entry per device
	spinlock_lock(&DeviceManager::internal::spl_devicequeue);
	FetchEntry entryFetcher;
	errorcode_t err = HandleReadDir(file, dirents, len, entryFetcher);
	spinlock_unlock(&DeviceManager::internal::spl_devicequeue);
	return err;
}

class DeviceSubSystem : public IAnkhSubSystem
{
public:
	errorcode_t HandleReadDir(struct VFS_FILE* file, void* dirents, size_t* len) override
	{
		return HandleReadDir_Device(file, dirents, len);
	}

	errorcode_t FillInode(struct VFS_INODE* inode, ino_t inum) override
	{
		// Special-case the non-device entries; we can't look them up
		if (inum_to_id(inum) == 0) {
			switch(inum_to_sub(inum)) {
				case Devices::subRoot:
					inode->i_sb.st_mode |= S_IFDIR;
					return ananas_success();
				case Devices::subDevices:
				case Devices::subDrivers:
					inode->i_sb.st_mode |= S_IFREG;
					return ananas_success();
			}
		}

		dev_t devno = static_cast<dev_t>(inum_to_id(inum));
		Device* device = DeviceManager::FindDevice(devno);
		if (device == nullptr)
			return ANANAS_ERROR(IO);

		if (device->GetCharDeviceOperations() != nullptr) {
			inode->i_sb.st_mode |= S_IFCHR;
		} else if (device->GetBIODeviceOperations() != nullptr) {
			inode->i_sb.st_mode |= S_IFBLK;
		}
		return ananas_success();
	}

	errorcode_t HandleRead(struct VFS_FILE* file, void* buf, size_t* len) override
	{
		ino_t inum = file->f_dentry->d_inode->i_inum;

		dev_t devno = static_cast<dev_t>(inum_to_id(inum));
		if (devno == 0) {
			char result[1024]; // XXX
			strcpy(result, "???");
			switch(inum_to_sub(inum)) {
				case Devices::subDevices: {
					char* r = result;
					spinlock_lock(&DeviceManager::internal::spl_devicequeue);
					LIST_FOREACH_IP(&DeviceManager::internal::deviceList, all, device, Device) {
						snprintf(r, sizeof(result) - (r - result), "%s %d %d %c%c%c%c%c\n",
						 device->d_Name, device->d_Major, device->d_Unit, 
						 device->GetBIODeviceOperations() != nullptr ? 'b' : '.',
						 device->GetCharDeviceOperations() != nullptr ? 'c' : '.',
						 device->GetUSBHubDeviceOperations() != nullptr ? 'h' : '.',
						 device->GetSCSIDeviceOperations() != nullptr ? 's' : '.',
						 device->GetUSBDeviceOperations() != nullptr ? 'u' : '.');
						r += strlen(r);
					}
					spinlock_unlock(&DeviceManager::internal::spl_devicequeue);
					break;
				}
				case Devices::subDrivers: {
					char* r = result;
					auto& driverList = Ananas::DriverManager::internal::GetDriverList();
					LIST_FOREACH_SAFE(&driverList, d, Driver) {
						snprintf(r, sizeof(result) - (r - result), "%s %d %d %d\n",
						 d->d_Name, d->d_Priority, d->d_Major, d->d_CurrentUnit);
						r += strlen(r);
					}
					break;
				}
			}
			return AnkhFS::HandleRead(file, buf, len, result);
		}

		// TODO
		return ANANAS_ERROR(IO);
	}
};

} // unnamed namespace

IAnkhSubSystem& GetDeviceSubSystem()
{
	static DeviceSubSystem deviceSubSystem;
	return deviceSubSystem;
}

} // namespace AnkhFS
} // namespace Ananas

/* vim:set ts=2 sw=2: */
