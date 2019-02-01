#include "KernelMemoryManager.h"
#include "ArchCommon.h"
#include "assert.h"
#include "debug_bochs.h"
#include "kprintf.h"
#include "debug.h"
#include "Scheduler.h"
#include "ArchInterrupts.h"
#include "ArchMemory.h"
#include "PageManager.h"
#include "kstring.h"
#include "Stabs2DebugInfo.h"
#include "backtrace.h"
#include "ArchMulticore.h"
extern Stabs2DebugInfo const* kernel_debug_info;

KernelMemoryManager kmm;

KernelMemoryManager * KernelMemoryManager::instance_;
size_t KernelMemoryManager::pm_ready_;

KernelMemoryManager* KernelMemoryManager::instance()
{
  if (unlikely(!instance_))
  {
    assert(false && "you can not use KernelMemoryManager::instance before the PageManager is ready!");
  }
  return instance_;
}


KernelMemoryManager::KernelMemoryManager(size_t min_heap_pages, size_t max_heap_pages) :
        tracing_(false), lock_("KMM::lock_"), segments_used_(0), segments_free_(0)
{
  debug(KMM, "Initializing KernelMemoryManager\n");

  assert(instance_ == 0);
  instance_ = this;

  pointer start_address = ArchCommon::getFreeKernelMemoryStart();
  assert(((start_address) % PAGE_SIZE) == 0);
  base_break_ = start_address;
  kernel_break_ = start_address + min_heap_pages * PAGE_SIZE;
  reserved_min_ = min_heap_pages * PAGE_SIZE;
  reserved_max_ = max_heap_pages * PAGE_SIZE;

  debug(KMM, "Clearing initial heap pages\n");
  memset((void*)start_address, 0, min_heap_pages * PAGE_SIZE);

  first_ = (MallocSegment*)start_address;
  new (first_) MallocSegment(0, 0, min_heap_pages * PAGE_SIZE - sizeof(MallocSegment), false);
  last_ = first_;

  debug(KMM, "KernelMemoryManager::ctor, Heap starts at %zx and initially ends at %zx\n", start_address, start_address + min_heap_pages * PAGE_SIZE);
}


pointer KernelMemoryManager::allocateMemory(size_t requested_size, pointer called_by)
{
  debug(KMM, "allocateMemory, size: %zx, called by: %zx\n", requested_size, called_by);
  assert((requested_size & 0x80000000) == 0 && "requested too much memory");

  // 16 byte alignment
  requested_size = (requested_size + 0xF) & ~0xF;

  lockKMM();
  pointer ptr = private_AllocateMemory(requested_size, called_by);
  if (ptr)
    unlockKMM();

  debug(KMM, "allocateMemory returns address: %zx \n", ptr);
  return ptr;
}


pointer KernelMemoryManager::private_AllocateMemory(size_t requested_size, pointer called_by)
{
  assert((requested_size & 0xF) == 0 && "Attempt to allocate block with unaligned size");

  // find next free pointer of neccessary size + sizeof(MallocSegment);
  MallocSegment *new_pointer = findFreeSegment(requested_size);

  if (new_pointer == 0)
  {
    unlockKMM();
    kprintfd("KernelMemoryManager::allocateMemory: Not enough Memory left\n");
    kprintfd("Are we having a memory leak in the kernel??\n");
    kprintfd(
        "This might as well be caused by running too many threads/processes, which partially reside in the kernel.\n");
    assert(false && "Kernel Heap is out of memory");
    return 0;
  }

  fillSegment(new_pointer, requested_size);
  new_pointer->freed_at_ = 0;
  new_pointer->alloc_at_ = tracing_ ? called_by : 0;
  new_pointer->alloc_by_ = (pointer)currentThread;

  return ((pointer) new_pointer) + sizeof(MallocSegment);
}

bool KernelMemoryManager::freeMemory(pointer virtual_address, pointer called_by)
{
  if (virtual_address == 0)
    return false;

  assert(virtual_address >= ((pointer) first_) && (virtual_address < kernel_break_) && "Invalid free of address not in kernel heap");

  lockKMM();

  MallocSegment *m_segment = getSegmentFromAddress(virtual_address);
  m_segment->checkCanary();

  freeSegment(m_segment, called_by);

  unlockKMM();
  return true;
}


pointer KernelMemoryManager::reallocateMemory(pointer virtual_address, size_t new_size, pointer called_by)
{
  assert((new_size & 0x80000000) == 0 && "requested too much memory");
  if (new_size == 0)
  {
    //in case you're wondering: we really don't want to lock here yet :) guess why
    freeMemory(virtual_address, called_by);
    return 0;
  }
  //iff the old segment is no segment ;) -> we create a new one
  if (virtual_address == 0)
    return allocateMemory(new_size, called_by);

  // 16 byte alignment
  new_size = (new_size + 0xF) & ~0xF;
  assert((new_size & 0xF) == 0 && "BUG: segment size must be aligned");

  lockKMM();

  MallocSegment *m_segment = getSegmentFromAddress(virtual_address);
  m_segment->checkCanary();
  assert(m_segment->getUsed());

  if (new_size == m_segment->getSize())
  {
    unlockKMM();
    return virtual_address;
  }

  if (new_size < m_segment->getSize())
  {
    fillSegment(m_segment, new_size, 0);
    unlockKMM();
    return virtual_address;
  }
  else
  {
    //maybe we can solve this the easy way...
    if (m_segment->next_ && !m_segment->next_->getUsed() && (m_segment->getSize() + m_segment->next_->getSize() >= new_size))
    {
      auto s = mergeSegments(m_segment, m_segment->next_);
      assert(s == m_segment);
      assert(m_segment->getSize() >= new_size);
      unlockKMM();
      return virtual_address;
    }

    //or not.. lets search for larger space

    //thx to Philipp Toeglhofer we are not going to deadlock here anymore ;)
    pointer new_address = private_AllocateMemory(new_size, called_by);
    if (new_address == 0)
    {
      //we are not freeing the old semgent in here, so that the data is not
      //getting lost, although we could not allocate more memory

      //just if you wonder: the KMM is already unlocked
      kprintfd("KernelMemoryManager::reallocateMemory: Not enough Memory left\n");
      kprintfd("Are we having a memory leak in the kernel??\n");
      kprintfd(
          "This might as well be caused by running too many threads/processes, which partially reside in the kernel.\n");
      assert(false && "Kernel Heap is out of memory");
      return 0;
    }
    memcpy((void*) new_address, (void*) virtual_address, m_segment->getSize());
    freeSegment(m_segment, called_by);
    unlockKMM();
    return new_address;
  }
}


MallocSegment *KernelMemoryManager::getSegmentFromAddress(pointer virtual_address)
{
  MallocSegment *m_segment;
  m_segment = (MallocSegment*) (virtual_address - sizeof(MallocSegment));
  assert(virtual_address != 0 && m_segment != 0 && "trying to access a nullpointer");
  m_segment->checkCanary();
  return m_segment;
}


MallocSegment *KernelMemoryManager::findFreeSegment(size_t requested_size)
{
  if(KMM & OUTPUT_ADVANCED)
    debug(KMM, "findFreeSegment: seeking memory block of bytes: %zd \n",
          requested_size + sizeof(MallocSegment));

  MallocSegment *current = first_;
  while (current != 0)
  {
    if(KMM & OUTPUT_ADVANCED)
      debug(KMM, "findFreeSegment: current: %p size: %zd used: %d \n",
            current, current->getSize() + sizeof(MallocSegment), current->getUsed());

    current->checkCanary();
    if ((current->getSize() >= requested_size) && (current->getUsed() == false))
      return current;

    current = current->next_;
  }

  // No free segment found, could we allocate more memory?
  if(last_->getUsed())
  {
    // In this case we have to create a new segment...
    MallocSegment* new_segment = new ((void*)ksbrk(sizeof(MallocSegment) + requested_size)) MallocSegment(last_, 0, requested_size, 0);
    last_->next_ = new_segment;
    last_ = new_segment;
  }
  else
  {
    // else we just increase the size of the last segment
    size_t needed_size = requested_size - last_->getSize();
    ksbrk(needed_size);
    last_->setSize(requested_size);
  }

  return last_;
}


void KernelMemoryManager::fillSegment(MallocSegment *this_one, size_t requested_size, uint32 zero_check)
{
  assert(this_one != 0 && "trying to access a nullpointer");
  this_one->checkCanary();
  assert(this_one->getSize() >= requested_size && "segment is too small for requested size");
  assert((requested_size & 0xF) == 0 && "Attempt to fill segment with unaligned size");

  size_t* mem = (size_t*) (this_one + 1);
  uint8* memb = (uint8*)mem;
  // sizeof(size_t) steps
  if (zero_check)
  {
    for (size_t i = 0; i < this_one->getSize() / sizeof(*mem); ++i)
    {
      if(unlikely(mem[i] != 0))
      {
        kprintfd("KernelMemoryManager::fillSegment: WARNING: Memory not zero at %p (value=%zx)\n", mem + i, mem[i]);
        if(this_one->freed_at_)
        {
          if(kernel_debug_info)
          {
            kprintfd("KernelMemoryManager::freeSegment: The chunk may previously be freed at: ");
            kernel_debug_info->printCallInformation(this_one->freed_at_);
          }
          assert(false);
        }
        mem[i] = 0;
      }
    }
    // handle remaining bytes
    for(size_t i = this_one->getSize() - (this_one->getSize() % sizeof(*mem)); i < this_one->getSize(); ++i)
    {
      if(unlikely(memb[i] != 0))
      {
        kprintfd("KernelMemoryManager::fillSegment: WARNING: Memory not zero at %p (value=%x)\n", memb + i, memb[i]);
        if(this_one->freed_at_)
        {
          if(kernel_debug_info)
          {
            kprintfd("KernelMemoryManager::freeSegment: The chunk may previously be freed at: ");
            kernel_debug_info->printCallInformation(this_one->freed_at_);
          }
          assert(false);
        }
        mem[i] = 0;
      }
    }
  }

  //size stays as it is, if there would be no more space to add a new segment
  this_one->setUsed(true);
  assert(this_one->getUsed() == true && "trying to fill an unused segment");

  //add a free segment after this one, if there's enough space
  size_t space_left = this_one->getSize() - requested_size;
  if (space_left > sizeof(MallocSegment))
  {
    this_one->setSize(requested_size);
    assert(this_one->getSize() == requested_size && "size error");
    assert(this_one->getUsed() == true && "trying to fill an unused segment");

    MallocSegment *new_segment =
            new ((void*) (((pointer)(this_one + 1)) + requested_size)) MallocSegment(
                    this_one, this_one->next_, space_left - sizeof(MallocSegment), false);
    if (this_one->next_ != 0)
    {
      this_one->next_->checkCanary();
      this_one->next_->prev_ = new_segment;
    }
    this_one->next_ = new_segment;

    if (new_segment->next_ == 0)
      last_ = new_segment;

  }
  debug(KMM, "fillSegment: filled memory block of bytes: %zd \n", this_one->getSize() + sizeof(MallocSegment));
}


void KernelMemoryManager::freeSegment(MallocSegment *this_one, pointer called_by)
{
  debug(KMM, "KernelMemoryManager::freeSegment(%p)\n", this_one);
  assert(this_one != 0 && "trying to access a nullpointer");
  this_one->checkCanary();

  if (this_one->getUsed() == false)
  {
    kprintfd("KernelMemoryManager::freeSegment: FATAL ERROR\n");
    kprintfd("KernelMemoryManager::freeSegment: tried freeing not used memory block\n");
    if(this_one->freed_at_)
    {
      if(kernel_debug_info)
      {
        kprintfd("KernelMemoryManager::freeSegment: The chunk may previously be freed at: ");
        kernel_debug_info->printCallInformation(this_one->freed_at_);
      }
    }
    assert(false);
  }

  debug(KMM, "freeSegment: freeing block: %p of bytes: %zd \n",
        this_one, this_one->getSize() + sizeof(MallocSegment));

  this_one->setUsed(false);
  this_one->freed_at_ = called_by;

  this_one = mergeSegments(this_one, this_one->prev_);
  this_one = mergeSegments(this_one, this_one->next_);

  memset((void*)(this_one + 1), 0, this_one->getSize()); // ease debugging

  // Change break if this is the last segment
  if(this_one == last_)
  {
    if(this_one != first_)
    {
      // Default case, there are three sub cases
      // 1. we can free the whole segment because it is above the reserved minimum
      // 2. we can not touch the segment because it is below the reserved minimum
      // 3. we can shrink the size of the segment because a part of it is above the reserved minimum
      if((size_t)this_one > base_break_ + reserved_min_)
      {
        // Case 1
        assert(this_one && this_one->prev_);
        this_one->prev_->checkCanary();
        this_one->prev_->next_ = 0;
        last_ = this_one->prev_;
        ksbrk(-(this_one->getSize() + sizeof(MallocSegment)));
      }
      else if((size_t)this_one + sizeof(MallocSegment) + this_one->getSize() <= base_break_ + reserved_min_)
      {
        // Case 2
        // This is easy, just relax and do nothing
      }
      else
      {
        // Case 3
        // First calculate the new size of the segment
        size_t segment_size = (base_break_ + reserved_min_) - ((size_t)this_one + sizeof(MallocSegment));
        // Calculate how much we have to sbrk
        ssize_t sub = segment_size - this_one->getSize();
        ksbrk(sub);
        this_one->setSize(segment_size);
      }
    }
    else
    {
      if((this_one->getSize() - reserved_min_))
      {
        ksbrk(-(this_one->getSize() - reserved_min_));
        this_one->setSize(reserved_min_);
      }
    }
  }

  {
    MallocSegment *current = first_;
    while (current != 0)
    {
      if(KMM & OUTPUT_ADVANCED)
              debug(KMM, "freeSegment: current: %p prev: %p next: %p size: %zd used: %d\n", current, current->prev_,
            current->next_, current->getSize() + sizeof(MallocSegment), current->getUsed());
      current->checkCanary();
      current = current->next_;
    }
  }
}

pointer KernelMemoryManager::ksbrk(ssize_t size)
{
  assert(base_break_ <= (size_t)kernel_break_ + size && "kernel heap break value corrupted");
  assert((((kernel_break_ - base_break_) + size) <= reserved_max_) && "maximum kernel heap size reached");
  assert(DYNAMIC_KMM && "ksbrk should only be called if DYNAMIC_KMM is 1 - not in baseline SWEB");
  if(size != 0)
  {
    size_t old_brk = kernel_break_;
    size_t cur_top_vpn = kernel_break_ / PAGE_SIZE;
    if ((kernel_break_ % PAGE_SIZE) == 0)
      cur_top_vpn--;
    kernel_break_ = ((size_t)kernel_break_) + size;
    size_t new_top_vpn = (kernel_break_ )  / PAGE_SIZE;
    if ((kernel_break_ % PAGE_SIZE) == 0)
      new_top_vpn--;
    if(size > 0)
    {
      debug(KMM, "%zx != %zx\n", cur_top_vpn, new_top_vpn);
      while(cur_top_vpn != new_top_vpn)
      {
        debug(KMM, "%zx != %zx\n", cur_top_vpn, new_top_vpn);
        cur_top_vpn++;
        assert(pm_ready_ && "Kernel Heap should not be used before PageManager is ready");
        size_t new_page = PageManager::instance()->allocPPN();
        if(unlikely(new_page == 0))
        {
          debug(KMM, "KernelMemoryManager::freeSegment: FATAL ERROR\n");
          debug(KMM, "KernelMemoryManager::freeSegment: no more physical memory\n");
          assert(new_page != 0 && "Kernel Heap is out of memory");
        }
        debug(KMM, "kbsrk: map %zx -> %zx\n", cur_top_vpn, new_page);
        ArchMemory::mapKernelPage(cur_top_vpn, new_page);
      }

    }
    else
    {
      while(cur_top_vpn != new_top_vpn)
      {
        assert(pm_ready_ && "Kernel Heap should not be used before PageManager is ready");
        ArchMemory::unmapKernelPage(cur_top_vpn);
        cur_top_vpn--;
      }
    }
    return old_brk;
  }
  else
  {
    return kernel_break_;
  }
}


Thread* KernelMemoryManager::KMMLockHeldBy()
{
  return lock_.heldBy();
}


void KernelMemoryManager::lockKMM()
{
  if(system_state == RUNNING)
  {
    debug(KMM, "CPU %zx lock KMM\n", ArchMulticore::getCpuID());
  }
  assert(!((system_state == RUNNING) && (!ArchInterrupts::testIFSet())));
  lock_.acquire(getCalledBefore(1));
}


void KernelMemoryManager::unlockKMM()
{
  if(system_state == RUNNING)
  {
          debug(KMM, "CPU %zx unlock KMM\n", ArchMulticore::getCpuID());
  }
  lock_.release(getCalledBefore(1));
}


SpinLock& KernelMemoryManager::getKMMLock()
{
  return lock_;
}


size_t KernelMemoryManager::getUsedKernelMemory(bool show_allocs = false) {
    MallocSegment *current = first_;
    size_t size = 0, blocks = 0, unused = 0;
    if(show_allocs) kprintfd("Kernel Memory Usage\n\n");
    while (current != 0)
    {
      if (current->getUsed()) {
        size += current->getSize();
        blocks++;
        if(current->alloc_at_ && show_allocs)
        {
            if(kernel_debug_info)
            {
                kprintfd("%8zu bytes (by %p) at: ", current->getSize(), (void*)current->alloc_by_);
                kernel_debug_info->printCallInformation(current->alloc_at_);
            }
        }
      } else {
          unused += current->getSize();
      }

      current = current->next_;
    }
    if(show_allocs) kprintfd("\n%zu bytes in %zu blocks are in use (%zu%%)\n", size, blocks, 100 * size / (size + unused));
    return size;
}


void KernelMemoryManager::startTracing() {
    tracing_ = true;
}


void KernelMemoryManager::stopTracing() {
    tracing_ = false;
}


bool MallocSegment::checkCanary()
{
  if(marker_ != 0xdeadbeef)
  {
    kprintfd("Memory corruption in KMM segment %p, size: %zx, marker: %x at %p, alloc at: %zx, freed at: %zx\n",
             this, getSize(), marker_, &marker_, alloc_at_, freed_at_);
    if(freed_at_)
    {
      if(kernel_debug_info)
      {
        kprintfd("The segment may have previously been freed at: ");
        kernel_debug_info->printCallInformation(freed_at_);
      }
    }
    assert(false && "memory corruption - probably 'write after delete'");
  }
  return true;
}



MallocSegment* KernelMemoryManager::mergeSegments(MallocSegment* s1, MallocSegment* s2)
{
        assert(s1);
        if(s1) s1->checkCanary();
        if(s2) s2->checkCanary();

        if(s1 == s2) return s1;
        if(!s2) return s1;

        // Only merge if the segment we want to merge with is not in use
        if(s2->getUsed()) return s1;

        if(s2 < s1)
        {
                MallocSegment* tmp = s1;
                s1 = s2;
                s2 = tmp;
        }

        // Can merge a used segment with an unused one following it but not the other way around
        assert(!s2->getUsed());

        size_t s2_true_size = (s2->next_ ? (pointer)s2->next_ : kernel_break_) - (pointer)s2;
        debug(KMM, "mergeSegments %p [%zx] + %p [%zx] => %p [%zx]\n",
              s1, s1->getSize() + sizeof(*s1), s2, s2->getSize() + sizeof(*s2),
              s1, sizeof(*s1) + s1->getSize() + s2_true_size);


        assert(s1->next_ == s2);
        assert(((pointer)(s1+1) + s1->getSize()) == (pointer)s2);


        s1->setSize(s1->getSize() + s2_true_size);
        s1->next_ = s2->next_;
        if(s2->next_)
        {
                s2->next_->checkCanary();
                s2->next_->prev_ = s1;
        }
        else
        {
                assert(s2 == last_ && "this should never happen, there must be a bug in KMM");
                last_ = s1;
        }

        memset(s2, 0, sizeof(*s2));

        return s1;
}
