#include <exceptions.h>

#define ZERO_EXCEPTION_ERR_MSG "Se produjo una excepcion por division por cero!\n"

void zero_division(uint64_t * registerDumpPos) {
	default_exception_handler(ZERO_EXCEPTION_ERR_MSG, registerDumpPos);
}
