#include "stdarg.h"
#include "kprintf.h"
#include "Console.h"
#include "Terminal.h"
#include "debug_bochs.h"
#include "ArchInterrupts.h"
#include "ArchMulticore.h"
#include "ArchCommon.h"
#include "RingBuffer.h"
#include "Scheduler.h"
#include "assert.h"
#include "debug.h"
#include "ustringformat.h"

//it's more important to keep the messages that led to an error, instead of
//the ones following it, when the nosleep buffer gets full

RingBuffer<char> *nosleep_rb_;
Thread *flush_thread_;

void flushActiveConsole()
{
  assert(main_console);
  assert(nosleep_rb_);
  assert(ArchInterrupts::testIFSet());
  char c = 0;
  while (nosleep_rb_->get(c))
  {
    main_console->getActiveTerminal()->write(c);
  }
  Scheduler::instance()->yield();
}

class KprintfFlushingThread : public Thread
{
  public:

    KprintfFlushingThread() : Thread(0, "KprintfFlushingThread", Thread::KERNEL_THREAD)
    {
    }

    virtual void Run()
    {
      while (true)
      {
        flushActiveConsole();
      }
    }
};

static uint8 fb_row = 0;
static uint8 fb_col = 0;
static bool kprintf_initialized = false;

static void setFBrow(uint8 row)
{
        fb_row = row;
}
static void setFBcol(uint8 col)
{
        fb_col = col;
}
static uint8 getFBrow()
{
        return fb_row;
}
static uint8 getFBcol()
{
        return fb_col;
}
static uint8 getNextFBrow()
{
        return (getFBrow() == 24 ? 0 : getFBrow() + 1);
}

static char* getFBAddr(uint8 row, uint8 col)
{
        return (char*)ArchCommon::getFBPtr() + ((row*80 + col) * 2);
}

static void clearFBrow(uint8 row)
{
        memset(getFBAddr(row, 0), 0, 80 * 2);
}

static void FBnewline()
{
        uint8 next_row = getNextFBrow();
        clearFBrow(next_row);
        setFBrow(next_row);
        setFBcol(0);
}

static void kputc(const char c)
{
        if(c == '\n')
        {
                FBnewline();
        }
        else
        {
                if(getFBcol() == 80)
                {
                        FBnewline();
                }

                uint32 row = getFBrow();
                uint32 col = getFBcol();

                char* fb_pos = getFBAddr(row, col);
                fb_pos[0] = c;
                fb_pos[1] = 0x02;

                setFBcol(getFBcol() + 1);
        }
}

void kprintf_init()
{
  nosleep_rb_ = new RingBuffer<char>(1024);
  debug(KPRINTF, "Adding Important kprintf Flush Thread\n");
  flush_thread_ = new KprintfFlushingThread();
  Scheduler::instance()->addNewThread(flush_thread_);
  kprintf_initialized = true;
}

void kprintf_func(int ch, void *arg __attribute__((unused)))
{
  if(kprintf_initialized)
  {
    //check if atomar or not in current context
    if((system_state == RUNNING) && ArchInterrupts::testIFSet() && Scheduler::instance()->isSchedulingEnabled())
    {
      main_console->getActiveTerminal()->write(ch);
    }
    else
    {
      nosleep_rb_->put(ch);
    }
  }
  else
  {
    kputc(ch); // Used while booting
  }
}

void kprintf(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  kvprintf(fmt, kprintf_func, 0, 10, args);
  va_end(args);
}

void kprintfd_func(int ch, void *arg __attribute__((unused)))
{
  writeChar2Bochs((uint8) ch);
}

void kprintfd(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  kvprintf(fmt, kprintfd_func, 0, 10, args);
  va_end(args);
}
