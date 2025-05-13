#include "ArchCommon.h"

#include "FrameBufferConsole.h"
#include "IDEDriver.h"
#include "KernelMemoryManager.h"
#include "PageManager.h"
#include "PlatformBus.h"
#include "ProgrammableIntervalTimer.h"
#include "SMP.h"
#include "SWEBDebugInfo.h"
#include "Scheduler.h"
#include "SerialManager.h"
#include "Stabs2DebugInfo.h"
#include "TextConsole.h"
#include "backtrace.h"
#include "debug_bochs.h"
#include "kprintf.h"
#include "kstring.h"
#include "multiboot.h"
#include "offsets.h"
#include "ports.h"
#include "Console.h"

#include "ArchMemory.h"
#include "ArchMulticore.h"

#if (A_BOOT == A_BOOT | OUTPUT_ENABLED)
#define PRINT(X) writeLine2Bochs((const char*)VIRTUAL_TO_PHYSICAL_BOOT(X))
#else
#define PRINT(X)
#endif

extern Console *main_console;

RangeAllocator<> mmio_addr_allocator;

extern void* kernel_start_address;
extern void* kernel_end_address;

multiboot_info_t* multi_boot_structure_pointer = (multiboot_info_t*)0xDEADDEAD; // must not be in bss segment
static struct multiboot_remainder mbr __attribute__ ((section (".data"))); // must not be in bss segment

extern "C" void parseMultibootHeader()
{
  uint32 i;

  multiboot_info_t *mb_infos = *(multiboot_info_t**)VIRTUAL_TO_PHYSICAL_BOOT( (pointer)&multi_boot_structure_pointer);

  struct multiboot_remainder &orig_mbr = (struct multiboot_remainder &)(*((struct multiboot_remainder*)VIRTUAL_TO_PHYSICAL_BOOT((pointer)&mbr)));

  PRINT("Bootloader: ");
  writeLine2Bochs((char*)(pointer)(mb_infos->boot_loader_name));
  PRINT("\n");

  if (mb_infos && mb_infos->f_cmdline)
  {
    const char* cmdline = (char*)(uintptr_t)mb_infos->cmdline;
    size_t len = strlen(cmdline);
    if (len+1 <= sizeof(orig_mbr.cmdline))
    {
        memcpy(orig_mbr.cmdline, cmdline, len+1);
    }
  }

  if (mb_infos && mb_infos->f_fb)
  {
    orig_mbr.have_framebuffer = true;
    orig_mbr.framebuffer = mb_infos->framebuffer;
  }

  if (mb_infos && mb_infos->f_vbe)
  {
    orig_mbr.have_vbe = true;
    orig_mbr.vbe = mb_infos->vbe;
    struct vbe_mode* mode_info = (struct vbe_mode*)mb_infos->vbe.vbe_mode_info;
    orig_mbr.have_vesa_console = 1;
    orig_mbr.vesa_lfb_pointer = mode_info->phys_base;
    orig_mbr.vesa_x_res = mode_info->x_resolution;
    orig_mbr.vesa_y_res = mode_info->y_resolution;
    orig_mbr.vesa_bits_per_pixel = mode_info->bits_per_pixel;
  }

  if (mb_infos && mb_infos->f_mods)
  {
    module_t * mods = (module_t*)mb_infos->mods_addr;
    for (i=0;i<mb_infos->mods_count;++i)
    {
      orig_mbr.module_maps[i].used = 1;
      orig_mbr.module_maps[i].start_address = mods[i].mod_start;
      orig_mbr.module_maps[i].end_address = mods[i].mod_end;
      strncpy((char*)(uint32)orig_mbr.module_maps[i].name, (const char*)(uint32)mods[i].string, 256);
      PRINT("Module: ");
      writeLine2Bochs((char*)mods[i].string);
      PRINT("\n");
    }
    orig_mbr.num_module_maps = mb_infos->mods_count;
  }

  for (i=0;i<MAX_MEMORY_MAPS;++i)
  {
    orig_mbr.memory_maps[i].used = 0;
  }

  if (mb_infos && mb_infos->f_mmap)
  {
    size_t i = 0;
    memory_map * map = (memory_map*)(uint64)(mb_infos->mmap_addr);
    while((uint64)map < (uint64)(mb_infos->mmap_addr + mb_infos->mmap_length))
    {
      orig_mbr.memory_maps[i].used          = 1;
      orig_mbr.memory_maps[i].start_address = ((uint64)map->base_addr_high << 32) | ((uint64)map->base_addr_low);
      orig_mbr.memory_maps[i].end_address   = orig_mbr.memory_maps[i].start_address
              + (((uint64)map->length_high << 32) | ((uint64)map->length_low));
      orig_mbr.memory_maps[i].type          = map->type;

      map = (memory_map*)(((uint64)(map)) + map->size + sizeof(map->size));
      ++i;
    }
  }

  if (mb_infos && mb_infos->f_elf_shdr)
  {
    orig_mbr.have_elf_sec_hdr = true;
    orig_mbr.elf_sec = mb_infos->elf_sec;
  }
}

pointer ArchCommon::getKernelStartAddress()
{
   return (pointer)&kernel_start_address;
}

pointer ArchCommon::getKernelEndAddress()
{
   return (pointer)&kernel_end_address;
}

pointer ArchCommon::getFreeKernelMemoryStart()
{
   pointer free_kernel_memory_start = (pointer)&kernel_end_address;
   for (size_t i = 0; i < getNumModules(); ++i)
           free_kernel_memory_start = Max(getModuleEndAddress(i), free_kernel_memory_start);
   return ((free_kernel_memory_start - 1) | 0xFFF) + 1;
}

pointer ArchCommon::getFreeKernelMemoryEnd()
{
   return (pointer)(1024U*1024U*1024U*2U + 1024U*1024U*4U); //2GB+4MB Ende des Kernel Bereichs fuer den es derzeit Paging gibt
}


uint32 ArchCommon::haveVESAConsole(uint32 is_paging_set_up)
{
  if (is_paging_set_up)
    return mbr.have_vesa_console;
  else
  {
    struct multiboot_remainder &orig_mbr = (struct multiboot_remainder &)(*((struct multiboot_remainder*)VIRTUAL_TO_PHYSICAL_BOOT((pointer)&mbr)));
    return orig_mbr.have_vesa_console;
  }
}

uint32 ArchCommon::getNumModules(uint32 is_paging_set_up)
{
  if (is_paging_set_up)
    return mbr.num_module_maps;
  else
  {
    struct multiboot_remainder &orig_mbr = (struct multiboot_remainder &)(*((struct multiboot_remainder*)VIRTUAL_TO_PHYSICAL_BOOT((pointer)&mbr)));
    return orig_mbr.num_module_maps;
  }

}

const char* ArchCommon::getModuleName(size_t num, size_t is_paging_set_up)
{
  if (is_paging_set_up)
    return (char*)((size_t)mbr.module_maps[num].name);
  else
  {
    struct multiboot_remainder &orig_mbr = (struct multiboot_remainder &)(*((struct multiboot_remainder*)VIRTUAL_TO_PHYSICAL_BOOT((pointer)&mbr)));
    return (char*)orig_mbr.module_maps[num].name;
  }
}

uint32 ArchCommon::getModuleStartAddress(uint32 num, uint32 is_paging_set_up)
{
  if (is_paging_set_up)
    return mbr.module_maps[num].start_address + PHYSICAL_TO_VIRTUAL_OFFSET;
  else
  {
    struct multiboot_remainder &orig_mbr = (struct multiboot_remainder &)(*((struct multiboot_remainder*)VIRTUAL_TO_PHYSICAL_BOOT((pointer)&mbr)));
    return orig_mbr.module_maps[num].start_address;
  }

}

uint32 ArchCommon::getModuleEndAddress(uint32 num, uint32 is_paging_set_up)
{
  if (is_paging_set_up)
    return mbr.module_maps[num].end_address + PHYSICAL_TO_VIRTUAL_OFFSET;
  else
  {
    struct multiboot_remainder &orig_mbr = (struct multiboot_remainder &)(*((struct multiboot_remainder*)VIRTUAL_TO_PHYSICAL_BOOT((pointer)&mbr)));
    return orig_mbr.module_maps[num].end_address;
  }

}

uint32 ArchCommon::getVESAConsoleHeight()
{
  return mbr.vesa_y_res;
}

uint32 ArchCommon::getVESAConsoleWidth()
{
  return mbr.vesa_x_res;
}

pointer ArchCommon::getVESAConsoleLFBPtr(uint32 is_paging_set_up)
{
  if (is_paging_set_up)
    return 1024U*1024U*1024U*3U - 1024U*1024U*16U;
  else
  {
    struct multiboot_remainder &orig_mbr = (struct multiboot_remainder &)(*((struct multiboot_remainder*)VIRTUAL_TO_PHYSICAL_BOOT((pointer)&mbr)));
    return orig_mbr.vesa_lfb_pointer;
  }
}

pointer ArchCommon::getFBPtr(uint32 is_paging_set_up)
{
  if (is_paging_set_up)
    return 0xC00B8000;
  else
    return 0x000B8000;
}

size_t ArchCommon::getFBWidth()
{
    return 80;
}

size_t ArchCommon::getFBHeight()
{
    return 25;
}

size_t ArchCommon::getFBBitsPerCharacter()
{
    return 16;
}

size_t ArchCommon::getFBSize()
{
    return getFBWidth() * getFBHeight() * getFBBitsPerCharacter()/8;
}

uint32 ArchCommon::getVESAConsoleBitsPerPixel()
{
  return mbr.vesa_bits_per_pixel;
}

uint32 ArchCommon::getNumUseableMemoryRegions()
{
  uint32 i;
  for (i=0;i<MAX_MEMORY_MAPS;++i)
  {
    if (!mbr.memory_maps[i].used)
      break;
  }
  return i;
}

uint32 ArchCommon::getUseableMemoryRegion(uint32 region, pointer &start_address, pointer &end_address, uint32 &type)
{
  if (region >= MAX_MEMORY_MAPS)
    return 1;

  start_address = mbr.memory_maps[region].start_address;
  end_address = mbr.memory_maps[region].end_address;
  type = mbr.memory_maps[region].type;

  return 0;
}

Console* ArchCommon::createConsole(uint32 count)
{
  // deactivate cursor
  outportb(0x3d4, 0xa);
  outportb(0x3d5, 0b00100000);
  if (haveVESAConsole())
    return new FrameBufferConsole(count);
  else
    return new TextConsole(count);
}

const Stabs2DebugInfo* kernel_debug_info = 0;

void ArchCommon::initDebug()
{
    debug(A_COMMON, "initDebug\n");
    for (size_t i = 0; i < getNumModules(); ++i)
    {
        debug(A_COMMON, "Checking module from [%zx -> %zx)\n", getModuleStartAddress(i), getModuleEndAddress(i));
        if ((getModuleStartAddress(i) < getModuleEndAddress(i)) &&
            (memcmp("SWEBDBG1", (const char*)getModuleStartAddress(i), 8) == 0))
        {
            debug(A_COMMON, "Found SWEBDBG\n");
            kernel_debug_info = new SWEBDebugInfo((const char*)getModuleStartAddress(i),
                                                  (const char*)getModuleEndAddress(i));
        }
    }
    if (!kernel_debug_info)
    {
        kernel_debug_info = new SWEBDebugInfo(0, 0);
    }
    debug(A_COMMON, "initDebug done\n");
}

void ArchCommon::idle()
{
  halt();
}

void ArchCommon::halt()
{
  asm volatile("hlt");
}

uint64 ArchCommon::cpuTimestamp()
{
    uint64 timestamp;
    asm volatile("rdtsc\n"
                 :"=A"(timestamp));
    return timestamp;
}

#define STATS_START (11)
#define STATS_FREE_PAGES_OFFSET (11)

void ArchCommon::drawStat()
{
    const char* text = "Free pages      F9 MemInfo   F10 Locks   F11 Stacktrace   F12 Threads";
    const char* color = "xxxxxxxxxx      xx           xxx         xxx              xxx        ";

    char itoa_buffer[80];
    char* fb = (char*)getFBPtr();
    FrameBufferConsole* fb_console = static_cast<FrameBufferConsole*>(main_console);

    size_t row = 0;
    size_t column = STATS_START;

    for (size_t i = 0; text[i]; ++i)
    {
        char t = text[i];
        char c = (char)(color[i] == 'x' ? ((CONSOLECOLOR::BLACK) | (CONSOLECOLOR::DARK_GREY << 4))
                                        : ((CONSOLECOLOR::DARK_GREY) | (CONSOLECOLOR::BLACK << 4)));

        if (haveVESAConsole() && fb_console)
        {
            fb_console->consoleSetCharacter(row, column + i, t, c);
        }
        else
        {
            fb[row * 80 * 2 + column * 2 + i * 2] = t;
            fb[row * 80 * 2 + column * 2 + i * 2 + 1] = c;
        }
    }

    // ---

    row = 0;
    column = STATS_START + STATS_FREE_PAGES_OFFSET;

    memset(itoa_buffer, '\0', sizeof(itoa_buffer));
    itoa(PageManager::instance().getNumFreePages(), itoa_buffer, 10);

    if (haveVESAConsole() && fb_console)
    {
        for (size_t i = 0; i < 4; i++)
        {
            fb_console->consoleSetCharacter(row, column + i, 0, 0);
        }
    }
    else
    {
        memset(fb + row * 80 * 2 + column * 2, 0, 4 * 2);
    }

    for (size_t i = 0; (i < sizeof(itoa_buffer)) && (itoa_buffer[i] != '\0'); ++i)
    {
        char t = itoa_buffer[i];
        char c = ((CONSOLECOLOR::WHITE) | (CONSOLECOLOR::BLACK << 4));
        if (haveVESAConsole() && fb_console)
        {
            fb_console->consoleSetCharacter(row, column + i, itoa_buffer[i], c);
        }
        else
        {
            fb[row * 80 * 2 + column * 2 + i * 2] = t;
            fb[row * 80 * 2 + column * 2 + i * 2 + 1] = c;
        }
    }

    // ---

    row = 1;
    column = STATS_START + STATS_FREE_PAGES_OFFSET;

    size_t total_pages = PageManager::instance().getTotalNumPages();
    size_t free_pages_percent =
        total_pages ? (PageManager::instance().getNumFreePages() * 100) / total_pages : 0;

    memset(itoa_buffer, '\0', sizeof(itoa_buffer));
    itoa(free_pages_percent, itoa_buffer, 10);
    size_t free_pp_len = strlen(itoa_buffer);
    itoa_buffer[free_pp_len] = '%';

    if (haveVESAConsole() && fb_console)
    {
        for (size_t i = 0; i < 4; i++)
        {
            fb_console->consoleSetCharacter(row, column + i, 0, 0);
        }
    }
    else
    {
        memset(fb + row * 80 * 2 + column * 2, 0, 4 * 2);
    }

    for (size_t i = 0; (i < sizeof(itoa_buffer)) && (itoa_buffer[i] != '\0'); ++i)
    {
        char t = itoa_buffer[i];
        char c = ((CONSOLECOLOR::WHITE) | (CONSOLECOLOR::BLACK << 4));
        if (haveVESAConsole() && fb_console)
        {
            fb_console->consoleSetCharacter(row, column + i, itoa_buffer[i], c);
        }
        else
        {
            fb[row * 80 * 2 + column * 2 + i * 2] = t;
            fb[row * 80 * 2 + column * 2 + i * 2 + 1] = c;
        }
    }

    // ---

    row = 1;
    column = 73;

    memset(itoa_buffer, '\0', sizeof(itoa_buffer));
    itoa(Scheduler::instance()->num_threads, itoa_buffer, 10);

    if (haveVESAConsole() && fb_console)
    {
        for (size_t i = 0; i < 4; i++)
        {
            fb_console->consoleSetCharacter(row, column + i, 0, 0);
        }
    }
    else
    {
        memset(fb + row * 80 * 2 + column * 2, 0, 4 * 2);
    }

    for (size_t i = 0; (i < sizeof(itoa_buffer)) && (itoa_buffer[i] != '\0'); ++i)
    {
        char t = itoa_buffer[i];
        char c = ((CONSOLECOLOR::WHITE) | (CONSOLECOLOR::BLACK << 4));
        if (haveVESAConsole() && fb_console)
        {
            fb_console->consoleSetCharacter(row, column + i, itoa_buffer[i], c);
        }
        else
        {
            fb[row * 80 * 2 + column * 2 + i * 2] = t;
            fb[row * 80 * 2 + column * 2 + i * 2 + 1] = c;
        }
    }

    // ---

    row = 1;
    column = SMP::numRunningCpus();

    // calc fixnum xxx.xxx%
    size_t sched_lock_free = Scheduler::instance()->scheduler_lock_count_free;
    size_t sched_lock_blocked = Scheduler::instance()->scheduler_lock_count_blocked;
    size_t sched_lock_total = sched_lock_free + sched_lock_blocked;
    size_t sched_lock_contention_percent =
        sched_lock_total ? (sched_lock_blocked * 100) / sched_lock_total : 0;
    size_t sched_lock_contention_2 =
        sched_lock_total ? ((sched_lock_blocked * 100000) / sched_lock_total) % 1000 : 0;

    memset(itoa_buffer, '\0', sizeof(itoa_buffer));
    itoa(sched_lock_contention_percent, itoa_buffer, 10);
    size_t slc_len = strlen(itoa_buffer);
    itoa_buffer[slc_len++] = '.';
    if (sched_lock_contention_2 < 100)
        itoa_buffer[slc_len++] = '0';
    if (sched_lock_contention_2 < 10)
        itoa_buffer[slc_len++] = '0';
    itoa(sched_lock_contention_2, itoa_buffer + slc_len, 10);
    slc_len = strlen(itoa_buffer);
    itoa_buffer[slc_len] = '%';

    if (haveVESAConsole() && fb_console)
    {
        for (size_t i = 0; i < 7; i++)
        {
            fb_console->consoleSetCharacter(row, column + i, 0, 0);
        }
    }
    else
    {
        memset(fb + row * 80 * 2 + column * 2, 0, 7 * 2);
    }

    for (size_t i = 0; (i < sizeof(itoa_buffer)) && (itoa_buffer[i] != '\0'); ++i)
    {
        char t = itoa_buffer[i];
        char c = ((CONSOLECOLOR::WHITE) | (CONSOLECOLOR::BLACK << 4));
        if (haveVESAConsole() && fb_console)
        {
            fb_console->consoleSetCharacter(row, column + i, itoa_buffer[i], c);
        }
        else
        {
            fb[row * 80 * 2 + column * 2 + i * 2] = t;
            fb[row * 80 * 2 + column * 2 + i * 2 + 1] = c;
        }
    }
}

cpu_local size_t heart_beat_value = 0;

void ArchCommon::drawHeartBeat()
{
  drawStat();

  const char* clock = "/-\\|";
  char heartbeat_char = clock[heart_beat_value++ % 4];
  size_t cpu_id = SMP::currentCpuId();
  char color = (((currentThread ? currentThread->console_color :
                                  CONSOLECOLOR::BLACK) << 4) | CONSOLECOLOR::BRIGHT_WHITE);

  if (haveVESAConsole())
  {
    FrameBufferConsole* fb_console = static_cast<FrameBufferConsole*>(main_console);
    if(fb_console)
      fb_console->consoleSetCharacter(0, (uint32_t) cpu_id, heartbeat_char, color);
  }
  else
  {
    char* fb = (char*)getFBPtr();
    fb[cpu_id*2] = heartbeat_char;
    fb[cpu_id*2 + 1] = color;
  }
}




void ArchCommon::postBootInit()
{
        initACPI();
}


[[noreturn]] void ArchCommon::callWithStack(char* stack, void (*func)())
{
        asm volatile("movl %[stack], %%esp\n"
                     "calll *%[func]\n"
                     ::[stack]"r"(stack),
                      [func]"r"(func));
        assert(false);
}


void ArchCommon::spinlockPause()
{
    asm volatile("pause\n");
}

void ArchCommon::reservePagesPreKernelInit(Allocator &alloc)
{
    ArchMulticore::reservePages(alloc);
}

void ArchCommon::initKernelVirtualAddressAllocator()
{
    mmio_addr_allocator.setUseable(KERNEL_START, (size_t)-1);
    mmio_addr_allocator.setUnuseable(getKernelStartAddress(), getKernelEndAddress());
    mmio_addr_allocator.setUnuseable(KernelMemoryManager::instance()->getKernelHeapStart(), KernelMemoryManager::instance()->getKernelHeapMaxEnd());
    mmio_addr_allocator.setUnuseable(IDENT_MAPPING_START, IDENT_MAPPING_END);
    debug(MAIN, "Usable MMIO ranges:\n");
    mmio_addr_allocator.printUsageInfo();
}

void ArchCommon::initBlockDeviceDrivers()
{
    PlatformBus::instance().registerDriver(IDEControllerDriver::instance());
}

void ArchCommon::initPlatformDrivers()
{
    PlatformBus::instance().registerDriver(PITDriver::instance());
    PlatformBus::instance().registerDriver(SerialManager::instance());
}

