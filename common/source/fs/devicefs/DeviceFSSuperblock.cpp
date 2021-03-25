#include "fs/devicefs/DeviceFSSuperblock.h"
#include "fs/ramfs/RamFSInode.h"
#include "DeviceFSType.h"
#include "fs/Dentry.h"
#include "fs/Inode.h"
#include "fs/File.h"
#include "fs/FileDescriptor.h"

#include "console/kprintf.h"

#include "Console.h"

class DeviceFSType;

extern Console* main_console;

const char DeviceFSSuperBlock::ROOT_NAME[] = "/";
const char DeviceFSSuperBlock::DEVICE_ROOT_NAME[] = "dev";

DeviceFSSuperBlock* DeviceFSSuperBlock::instance_ = 0;

DeviceFSSuperBlock::DeviceFSSuperBlock(DeviceFSType* fs_type, uint32 s_dev) :
    Superblock(fs_type, s_dev)
{
  // create the root folder
  Inode *root_inode = createInode(I_DIR);
  s_root_ = new Dentry(root_inode);
  assert(root_inode->mknod(s_root_) == 0);
}

DeviceFSSuperBlock::~DeviceFSSuperBlock()
{
  assert(dirty_inodes_.empty() == true);

  for (FileDescriptor* fd : s_files_)
  {
    delete fd->getFile();
    delete fd;
  }
  s_files_.clear();

  for (Inode* inode : all_inodes_)
  {
    delete inode->getDentry();
    delete inode;
  }
  all_inodes_.clear();
}

void DeviceFSSuperBlock::addDevice(Inode* device_inode, const char* node_name)
{
  // Devices are mounted at the devicefs root (s_root_)
  Dentry* fdntr = new Dentry(device_inode, s_root_, node_name);

  device_inode->mknod(fdntr);
  device_inode->setSuperBlock(this);
  all_inodes_.push_back(device_inode);
}

Inode* DeviceFSSuperBlock::createInode(uint32 type)
{
    auto inode = new RamFSInode(this, type);
    assert(inode);

    all_inodes_.push_back(inode);
    return inode;
}

int32 DeviceFSSuperBlock::createFd(Inode* inode, uint32 flag)
{
  assert(inode);

  File* file = inode->open(flag);
  FileDescriptor* fd = new FileDescriptor(file);
  s_files_.push_back(fd);
  FileDescriptor::add(fd);

  if (ustl::find(used_inodes_, inode) == used_inodes_.end())
  {
    used_inodes_.push_back(inode);
  }

  return (fd->getFd());
}


int32 DeviceFSSuperBlock::removeFd(Inode* inode, FileDescriptor* fd)
{
  assert(inode);
  assert(fd);

  s_files_.remove(fd);
  FileDescriptor::remove(fd);

  File* file = fd->getFile();
  int32 tmp = inode->release(file);

  debug(RAMFS, "remove the fd num: %d\n", fd->getFd());
  if (inode->getNumOpenedFile() == 0)
  {
    used_inodes_.remove(inode);
  }
  delete fd;

  return tmp;
}

DeviceFSSuperBlock* DeviceFSSuperBlock::getInstance()
{
    if (!instance_)
        instance_ = new DeviceFSSuperBlock(DeviceFSType::getInstance(), 0);
    return instance_;
}
