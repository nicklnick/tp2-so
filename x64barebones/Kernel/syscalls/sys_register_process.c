#include <multitasking.h>
#include <syscalls.h>

uint64_t sys_register_process(uint64_t entrypoint, int screen, uint64_t arg0)
{
	return add_task(entrypoint, screen, DEFAULT_PRIORITY, MORTAL, arg0);
}
