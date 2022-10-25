#include <multitasking.h>
#include <syscalls.h>

uint64_t sys_nice(uint8_t pid, int delta)
{
	return change_priority(pid, delta);
}
