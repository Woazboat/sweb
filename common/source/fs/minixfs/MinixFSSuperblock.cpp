#include "FileDescriptor.h"
#include "MinixFSSuperblock.h"
#include "MinixFSInode.h"
#include "MinixFSFile.h"
#include "Dentry.h"
#include "assert.h"
#include "kprintf.h"
#ifdef EXE2MINIXFS
#include <unistd.h>
#else
#include "kstring.h"
#include "BDManager.h"
#include "BDVirtualDevice.h"
#endif

#define ROOT_NAME "/"

MinixFSSuperblock::MinixFSSuperblock(Dentry* s_root, size_t s_dev, uint64 byte_offset) :
    Superblock(s_root, s_dev), superblock_(this)
{
  debug(M_SB, "Creating MinixFSSuperblock on device: %zu, offset: %llx\n", s_dev, byte_offset);
  offset_ = byte_offset;
  //read Superblock data from disc
  readHeader();
  debug(M_SB,
        "s_num_inodes_ : %d\n"
        "s_zones_ : %d\n"
        "s_num_inode_bm_blocks_ : %d\n"
        "s_num_zone_bm_blocks_ : %d\n"
        "s_1st_datazone_ : %d\n"
        "s_log_zone_size_ : %d\n"
        "s_max_file_size_ : %d\n"
        "s_block_size_ : %d\n"
        "s_magic_ : %llu\n",
        s_num_inodes_, s_zones_, s_num_inode_bm_blocks_, s_num_zone_bm_blocks_, s_1st_datazone_, s_log_zone_size_, s_max_file_size_, s_block_size_, (long long unsigned)s_magic_);
  assert(s_log_zone_size_ == 0);
  assert(s_block_size_ == 1024);
  //create Storage Manager
  uint32 bm_size = s_num_inode_bm_blocks_ + s_num_zone_bm_blocks_;
  char* bm_buffer = new char[BLOCK_SIZE * bm_size];
  readBlocks(2, bm_size, bm_buffer);
  debug(M_SB, "---creating Storage Manager\n");
  storage_manager_ = new MinixStorageManager(bm_buffer, s_num_inode_bm_blocks_, s_num_zone_bm_blocks_, s_num_inodes_, s_zones_);
  if (M_SB & OUTPUT_ENABLED)
  {
    storage_manager_->printBitmap();
  }
  delete[] bm_buffer;

  initInodes();
  debug(M_SB, "MinixFSSuperblock ctor finished\n");
}

void MinixFSSuperblock::readHeader()
{
  char buffer[BLOCK_SIZE];
  readBlocks(1, 1, buffer);

  MinixFSSuperblockOnDiskDataV3* v3_sb = (MinixFSSuperblockOnDiskDataV3*)&buffer;
  MinixFSSuperblockOnDiskDataV1* v1_sb = (MinixFSSuperblockOnDiskDataV1*)&buffer;

  if(v1_sb->s_magic == MINIX_V1)
  {
          s_magic_ = v1_sb->s_magic;
          debug(M_SB, "Found minixfs v1, magic value: %x\n", v1_sb->s_magic);
  }
  else if(v3_sb->s_magic == MINIX_V3)
  {
          s_magic_ = v3_sb->s_magic;
          debug(M_SB, "Found minixfs v3, magic value: %x\n", v3_sb->s_magic);
  }
  else
  {
          kprintfd("ERROR: Unknown magic value for minixfs superblock, V1: %x, V3: %x\n", v1_sb->s_magic, v3_sb->s_magic);
          assert(false && "Unknown minixfs version (or not minixfs at all?)");
  }

  s_num_inodes_ = (s_magic_ == MINIX_V3 ? v3_sb->s_num_inodes :
                  (s_magic_ == MINIX_V1 ? v1_sb->s_num_inodes :
                                          0));

  s_block_size_ = (s_magic_ == MINIX_V3 ? v3_sb->s_blocksize :
                  (s_magic_ == MINIX_V1 ? 1024 :
                                          0));

  s_disk_version_ = (s_magic_ == MINIX_V3 ? v3_sb->s_disk_version :
                    (s_magic_ == MINIX_V1 ? 0 :
                                            0));

  s_zones_ = (s_magic_ == MINIX_V3 ? v3_sb->s_num_zones :
             (s_magic_ == MINIX_V1 ? v1_sb->s_num_zones :
                                     0));

  s_num_inode_bm_blocks_ = (s_magic_ == MINIX_V3 ? v3_sb->s_imap_blocks :
                           (s_magic_ == MINIX_V1 ? v1_sb->s_imap_blocks :
                                                   0));

  s_num_zone_bm_blocks_ = (s_magic_ == MINIX_V3 ? v3_sb->s_zmap_blocks :
                          (s_magic_ == MINIX_V1 ? v1_sb->s_zmap_blocks :
                                                  0));

  s_1st_datazone_ = (s_magic_ == MINIX_V3 ? v3_sb->s_firstdatazone :
                    (s_magic_ == MINIX_V1 ? v1_sb->s_firstdatazone :
                                            0));

  s_log_zone_size_ = (s_magic_ == MINIX_V3 ? v3_sb->s_log_zone_size :
                     (s_magic_ == MINIX_V1 ? v1_sb->s_log_zone_size :
                                             0));

  s_max_file_size_ = (s_magic_ == MINIX_V3 ? v3_sb->s_max_file_size :
                     (s_magic_ == MINIX_V1 ? v1_sb->s_max_file_size :
                                             0));
}

void MinixFSSuperblock::initInodes()
{
  MinixFSInode *root_inode = getInode(1);
  debug(M_SB, "Creating root dentry\n");
  Dentry *root_dentry = new Dentry(ROOT_NAME);
  if (s_root_)
  {
    //root_dentry->setMountPoint(s_root_);
    s_root_->setMountPoint(root_dentry);
    mounted_over_ = s_root_; // MOUNT
    s_root_ = root_dentry;
  }
  else
  {
    mounted_over_ = root_dentry; // ROOT
    s_root_ = root_dentry;

  }
  root_dentry->setParent(root_dentry);
  root_inode->i_dentry_ = root_dentry;
  root_dentry->setInode(root_inode);

  all_inodes_add_inode(root_inode);
  //read children from disc
  root_inode->loadChildren();
}

MinixFSInode* MinixFSSuperblock::getInode(uint16 i_num, bool &is_already_loaded)
{
  MinixFSInode* tmp = (MinixFSInode*) all_inodes_set_[i_num];
  if (tmp)
  {
    is_already_loaded = true;
    return tmp;
  }
  tmp = getInode(i_num);
  return tmp;
}

MinixFSInode* MinixFSSuperblock::getInode(uint16 i_num)
{
  debug(M_SB, "getInode::called with i_num: %d\n", i_num);

  if (i_num >= storage_manager_->getNumUsedInodes())
  {
    debug(M_SB, "getInode::bad inode number %d\n", i_num);
    return 0;
  }

  if (!storage_manager_->isInodeSet(i_num))
  {
    if (i_num == 1)
      assert(storage_manager_->isInodeSet(1));

    return 0;
  }
  uint32 first_inode_block = 2 + s_num_inode_bm_blocks_ + s_num_zone_bm_blocks_;
  uint32 inode_block_num = first_inode_block + (i_num - 1) / INODES_PER_BLOCK;
  MinixFSInode *inode = 0;
  char ibuffer_array[BLOCK_SIZE];
  char* ibuffer = ibuffer_array;
  debug(M_SB, "getInode::reading block num: %d\n", inode_block_num);
  readBlocks(inode_block_num, 1, ibuffer);
  debug(M_SB, "getInode:: returned reading block num: %d\n", inode_block_num);
  uint32 byte_offset = ((i_num - 1) % INODES_PER_BLOCK) * INODE_SIZE;
  debug(M_SB, "getInode:: setting offset: %d\n", byte_offset);
  ibuffer += byte_offset;
  MinixFSInode::MinixFSInodeOnDiskDataV1* idata_v1 = (MinixFSInode::MinixFSInodeOnDiskDataV1*)ibuffer;
  MinixFSInode::MinixFSInodeOnDiskDataV3* idata_v3 = (MinixFSInode::MinixFSInodeOnDiskDataV3*)ibuffer;

  uint32 i_zones[NUM_ZONES];
  for (uint32 num_zone = 0; num_zone < NUM_ZONES; ++num_zone)
  {
          i_zones[num_zone] = (s_magic_ == MINIX_V3 ? idata_v3->i_zone[num_zone] :
                                                      idata_v1->i_zone[num_zone]);
  }

  debug(M_SB, "getInode:: calling creating Inode\n");
  uint16 i_mode = (s_magic_ == MINIX_V3 ? idata_v3->i_mode :
                                          idata_v1->i_mode);
  uint32 i_size = (s_magic_ == MINIX_V3 ? idata_v3->i_size :
                                          idata_v1->i_size);
  uint32 i_nlinks = (s_magic_ == MINIX_V3 ? idata_v3->i_nlinks :
                                            idata_v1->i_nlinks);
  inode = new MinixFSInode(this, i_mode, i_size, i_nlinks, i_zones, i_num);
  debug(M_SB, "getInode:: returned creating Inode\n");
  return inode;
}

MinixFSSuperblock::~MinixFSSuperblock()
{
  debug(M_SB, "~MinixSuperblock\n");
  assert(dirty_inodes_.empty() == true);
  storage_manager_->flush(this);
  for (FileDescriptor* fd : s_files_)
  {
    delete fd->getFile();
    delete fd;
  }
  s_files_.clear();
  assert(s_files_.empty() == true);

  if (M_SB & OUTPUT_ENABLED)
  {
    for (auto it : all_inodes_)
      debug(M_SB, "Inode: %p\n", it);
  }

  for (Inode* inode : all_inodes_)
  {
    debug(M_SB, "~MinixSuperblock writing inode to disc\n");
    writeInode(inode);

    debug(M_SB, "~MinixSuperblock inode written to disc\n");
    delete inode->getDentry();

    debug(M_SB, "~MinixSuperblock deleting inode\n");
    delete inode;
  }
  delete storage_manager_;

  all_inodes_.clear();
  all_inodes_set_.clear();

  debug(M_SB, "~MinixSuperblock finished\n");
}

Inode* MinixFSSuperblock::createInode(Dentry* dentry, uint32 type)
{
  uint16 mode = 0x01ff;
  if (type == I_FILE)
    mode |= 0x8000;
  else if (type == I_DIR)
    mode |= 0x4000;
  //else link etc.
  uint32 zones[NUM_ZONES];
  for (uint32 i = 0; i < NUM_ZONES; i++)
    zones[i] = 0;
  uint32 i_num = storage_manager_->allocInode();
  debug(M_SB, "createInode> allocated inode %d mode: %x\n", i_num, mode);
  Inode *inode = new MinixFSInode(this, mode, 0, 0, zones, i_num);
  debug(M_SB, "createInode> created Inode\n");
  all_inodes_add_inode(inode);
  debug(M_SB, "createInode> calling write Inode to Disc\n");
  writeInode(inode);
  debug(M_SB, "createInode> returned from write Inode to Disc\n");
  if (type == I_DIR)
  {
    debug(M_SB, "createInode> mkdir\n");
    int32 inode_init = inode->mkdir(dentry);
    assert(inode_init == 0);
  }
  else if (type == I_FILE)
  {
    debug(M_SB, "createInode> mkfile\n");
    int32 inode_init = inode->mkfile(dentry);
    assert(inode_init == 0);
  }
  debug(M_SB, "createInode> finished\n");
  return inode;
}

int32 MinixFSSuperblock::readInode(Inode* inode)
{
  assert(inode);
  MinixFSInode *minix_inode = (MinixFSInode *) inode;
  assert(ustl::find(all_inodes_.begin(), all_inodes_.end(), inode) != all_inodes_.end());
  uint32 block = 2 + s_num_inode_bm_blocks_ + s_num_zone_bm_blocks_
      + ((minix_inode->i_num_ - 1) * INODE_SIZE / BLOCK_SIZE);
  uint32 byte_offset = ((minix_inode->i_num_ - 1) * INODE_SIZE) % BLOCK_SIZE;
  char buffer[INODE_SIZE];
  readBytes(block, byte_offset, INODE_SIZE, buffer);
  uint32 *i_zones = new uint32[NUM_ZONES];
  for (uint32 num_zone = 0; num_zone < NUM_ZONES; num_zone++)
  {
    i_zones[num_zone] = V3_ARRAY(buffer, 7 - V3_OFFSET + num_zone);
  }
  MinixFSZone *to_delete_i_zones = minix_inode->i_zones_;
  minix_inode->i_zones_ = new MinixFSZone(this, i_zones);

  if (s_magic_ == MINIX_V3)
    minix_inode->i_nlink_ = ((uint16*)buffer)[1];
  else
    minix_inode->i_nlink_ = buffer[13];
  minix_inode->i_size_ = ((uint32*)buffer)[1 + V3_OFFSET];
  delete to_delete_i_zones;
  return 0;
}

void MinixFSSuperblock::writeInode(Inode* inode)
{
  assert(inode);
  assert(ustl::find(all_inodes_.begin(), all_inodes_.end(), inode) != all_inodes_.end());
  //flush zones
  MinixFSInode *minix_inode = (MinixFSInode *) inode;
  uint32 block = 2 + s_num_inode_bm_blocks_ + s_num_zone_bm_blocks_
      + ((minix_inode->i_num_ - 1) * INODE_SIZE / BLOCK_SIZE);
  uint32 byte_offset = ((minix_inode->i_num_ - 1) * INODE_SIZE) % BLOCK_SIZE;
  char buffer[INODE_SIZE];
  memset((void*) buffer, 0, sizeof(buffer));
  debug(M_SB, "writeInode> reading block %d with byte_offset %d from disc\n", block, byte_offset);
  readBytes(block, byte_offset, INODE_SIZE, buffer);
  debug(M_SB, "writeInode> read data from disc\n");
  debug(M_SB, "writeInode> the inode: i_type_: %d, i_nlink_: %d, i_size_: %d\n", minix_inode->i_type_,
        minix_inode->i_nlink_, minix_inode->i_size_);
  if (minix_inode->i_type_ == I_FILE)
  {
    debug(M_SB, "writeInode> setting mode to file : %x\n", *(uint16*) buffer | 0x81FF);
    *(uint16*) buffer |= 0x81FF;
  }
  else if (minix_inode->i_type_ == I_DIR)
  {
    debug(M_SB, "writeInode> setting mode to dir : %x\n", *(uint16*) buffer | 0x41FF);
    *(uint16*) buffer |= 0x41FF;
  }
  else
  {
    // link etc. unhandled
  }
  ((uint32*)buffer)[1+V3_OFFSET] = minix_inode->i_size_;
  if (s_magic_ == MINIX_V3)
    ((uint16*)buffer)[1] = minix_inode->i_nlink_;
  else
    buffer[13] = minix_inode->i_nlink_;
  debug(M_SB, "writeInode> writing bytes to disc on block %d with byte_offset %d\n", block, byte_offset);
  writeBytes(block, byte_offset, INODE_SIZE, buffer);
  debug(M_SB, "writeInode> flushing zones of inode %p\n", inode);
  minix_inode->i_zones_->flush(minix_inode->i_num_);
}

void MinixFSSuperblock::all_inodes_add_inode(Inode* inode)
{
  all_inodes_.push_back(inode);
  all_inodes_set_[((MinixFSInode*) inode)->i_num_] = inode;
}

void MinixFSSuperblock::all_inodes_remove_inode(Inode* inode)
{
  all_inodes_.remove(inode);
  all_inodes_set_.erase(((MinixFSInode*) inode)->i_num_);
}

void MinixFSSuperblock::delete_inode(Inode* inode)
{
  Dentry* dentry = inode->getDentry();
  assert(dentry == 0);
  assert(ustl::find(used_inodes_.begin(), used_inodes_.end(), inode) == used_inodes_.end());
  dirty_inodes_.remove(inode);
  MinixFSInode *minix_inode = (MinixFSInode *) inode;
  all_inodes_remove_inode(minix_inode);
  assert(minix_inode->i_files_.empty());
  minix_inode->i_zones_->freeZones();
  storage_manager_->freeInode(minix_inode->i_num_);
  uint32 block = 2 + s_num_inode_bm_blocks_ + s_num_zone_bm_blocks_
      + ((minix_inode->i_num_ - 1) * INODE_SIZE / BLOCK_SIZE);
  uint32 byte_offset = ((minix_inode->i_num_ - 1) * INODE_SIZE) % BLOCK_SIZE;
  char buffer[INODE_SIZE];
  memset((void*) buffer, 0, sizeof(buffer));
  writeBytes(block, byte_offset, INODE_SIZE, buffer);
  delete inode;
}

int32 MinixFSSuperblock::createFd(Inode* inode, uint32 flag)
{
  assert(inode);

  File* file = inode->link(flag);
  FileDescriptor* fd = new FileDescriptor(file);
  s_files_.push_back(fd);
  FileDescriptor::add(fd);

  if (ustl::find(used_inodes_.begin(), used_inodes_.end(), inode) == used_inodes_.end())
  {
    used_inodes_.push_back(inode);
  }

  return (fd->getFd());
}

int32 MinixFSSuperblock::removeFd(Inode* inode, FileDescriptor* fd)
{
  assert(inode);
  assert(fd);

  s_files_.remove(fd);
  FileDescriptor::remove(fd);

  File* file = fd->getFile();
  int32 tmp = inode->unlink(file);

  debug(M_SB, "remove the fd num: %d\n", fd->getFd());
  if (inode->getNumOpenedFile() == 0)
  {
    used_inodes_.remove(inode);
  }
  delete fd;

  return tmp;
}

uint16 MinixFSSuperblock::allocateZone()
{
  debug(M_SB, "MinixFSSuperblock allocateZone>\n");
  uint16 ret = (storage_manager_->allocZone() + s_1st_datazone_ - 1); // -1 because the zone nr 0 is set in the bitmap and should never be used!
  debug(M_SB, "MinixFSSuperblock allocateZone> returning %d\n", ret);
  return ret;
}

void MinixFSSuperblock::readZone(uint16 zone, char* buffer)
{
  assert(buffer);
  readBlocks(zone, ZONE_SIZE / BLOCK_SIZE, buffer);
}

void MinixFSSuperblock::readBlocks(uint16 block, uint32 num_blocks, char* buffer)
{
  assert(buffer);
#ifdef EXE2MINIXFS
  fseek((FILE*)s_dev_, offset_ + block * BLOCK_SIZE, SEEK_SET);
  assert(fread(buffer, 1, BLOCK_SIZE * num_blocks, (FILE*)s_dev_) == BLOCK_SIZE * num_blocks);
#else
  BDVirtualDevice* bdvd = BDManager::getInstance()->getDeviceByNumber(s_dev_);
  bdvd->readData(block * bdvd->getBlockSize(), num_blocks * bdvd->getBlockSize(), buffer);
#endif
}

void MinixFSSuperblock::writeZone(uint16 zone, char* buffer)
{
  writeBlocks(zone, ZONE_SIZE / BLOCK_SIZE, buffer);
}

void MinixFSSuperblock::writeBlocks(uint16 block, uint32 num_blocks, char* buffer)
{
#ifdef EXE2MINIXFS
  fseek((FILE*)s_dev_, offset_ + block * BLOCK_SIZE, SEEK_SET);
  assert(fwrite(buffer, 1, BLOCK_SIZE * num_blocks, (FILE*)s_dev_) == BLOCK_SIZE * num_blocks);
#else
  BDVirtualDevice* bdvd = BDManager::getInstance()->getDeviceByNumber(s_dev_);
  bdvd->writeData(block * bdvd->getBlockSize(), num_blocks * bdvd->getBlockSize(), buffer);
#endif
}

int32 MinixFSSuperblock::readBytes(uint32 block, uint32 byte_offset, uint32 size, char* buffer)
{
  assert(byte_offset+size <= BLOCK_SIZE);
  char rbuffer[BLOCK_SIZE];
  readBlocks(block, 1, rbuffer);
  memcpy(rbuffer + byte_offset, buffer, size);
  return size;
}

int32 MinixFSSuperblock::writeBytes(uint32 block, uint32 byte_offset, uint32 size, char* buffer)
{
  assert(byte_offset+size <= BLOCK_SIZE);
  char wbuffer[BLOCK_SIZE];
  readBlocks(block, 1, wbuffer);
  memcpy(wbuffer + byte_offset, buffer, size);
  writeBlocks(block, 1, wbuffer);
  return size;
}

void MinixFSSuperblock::freeZone(uint16 index)
{
  storage_manager_->freeZone(index - s_1st_datazone_ + 1);
}
