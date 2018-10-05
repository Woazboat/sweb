#include "nonstd.h"
#include "sys/syscall.h"
#include "../../../common/include/kernel/syscall-definitions.h"
#include "stdlib.h"

int createprocess(const char* path, int sleep)
{
  return __syscall(sc_createprocess, (long) path, sleep, 0x00, 0x00, 0x00);
}

int setValue(size_t key, size_t value)
{
        return __syscall(sc_set_value, (long) key, (long) value, 0x00, 0x00, 0x00);
}

int getValue(size_t key, size_t* value)
{
        return __syscall(sc_get_value, (long) key, (long) value, 0x00, 0x00, 0x00);
}

extern int main();

void _start()
{
  exit(main());
}
