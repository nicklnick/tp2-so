#include "../include/comandos.h"
#include "../include/syscalls.h"

void ps(){
	sys_list_process();
}