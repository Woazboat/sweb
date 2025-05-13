#include "fs/devicefs/DeviceFSSuperblock.h"

#include "BDManager.h"
#include "BDVirtualDevice.h"
#include "BlockDeviceInode.h"
#include "Console.h"
#include "Device.h"
#include "DeviceFSType.h"
#include "console/kprintf.h"
#include "fs/Dentry.h"
#include "fs/File.h"
#include "fs/FileDescriptor.h"
#include "fs/Inode.h"
#include "fs/ramfs/RamFSInode.h"

#include "debug.h"

class DeviceFSType;

extern Console* main_console;

const char DeviceFSSuperblock::ROOT_NAME[] = "/";
const char DeviceFSSuperblock::DEVICE_ROOT_NAME[] = "dev";

DeviceFSSuperblock* DeviceFSSuperblock::instance_ = nullptr;

DeviceFSSuperblock::DeviceFSSuperblock(DeviceFSType* fs_type, uint32 s_dev) :
    RamFSSuperblock(fs_type, s_dev)
{
}

DeviceFSSuperblock::~DeviceFSSuperblock()
{
}

void DeviceFSSuperblock::addDevice(Inode* device_inode, const char* device_name)
{
  // Devices are mounted at the devicefs root (s_root_)
  device_inode->setSuperblock(this);

  Dentry* fdntr = new Dentry(device_inode, s_root_, device_name);

  assert(device_inode->mknod(fdntr) == 0);
  all_inodes_.push_back(device_inode);
}

DeviceFSSuperblock* DeviceFSSuperblock::getInstance()
{
    if (!instance_)
        instance_ = new DeviceFSSuperblock(DeviceFSType::getInstance(), 0);
    return instance_;
}

void DeviceFSSuperblock::addBlockDeviceInodes()
{
    for (BDVirtualDevice* bdvd : BDManager::instance().device_list_)
    {
        debug(BD_VIRT_DEVICE, "Detected Device: %s :: %d\n", bdvd->getName(), bdvd->getDeviceNumber());
        kprintf("Detected Device: %s :: %d\n", bdvd->getName(), bdvd->getDeviceNumber());
        auto bdInode = new BlockDeviceInode(bdvd);
        addDevice(bdInode, bdvd->getName());
    }
}

void DeviceFSSuperblock::addDeviceInodes(Device& device_root)
{
    const auto rec_lambda = [this](Device& device, auto& rec_func) -> void
    {
        if (auto device_inode = device.deviceInode())
        {
            debug(DRIVER, "Device %s has inode, adding to devicefs\n",
                device.deviceName().c_str());
            addDevice(device_inode, device.deviceName().c_str());
        }
        for (Device* sd : device.subdevices())
        {
            rec_func(*sd, rec_func);
        }
    };
    rec_lambda(device_root, rec_lambda);
}
