#include <memoryManager.h>

unsigned int sys_free(void * ptr) {
	mm_free(ptr);

	return 1;	
}