#include <multitasking.h>

// ---- Constantes ----
#define TOTAL_TASKS 20
#define STACK_SIZE 2000

#define MAX_PRIORITY 5

#define NULL 0 // !!!! REMOVE !!!!

// ----- Estado de task -----
#define DEAD_PROCESS 0
#define ACTIVE_PROCESS 1 
#define PAUSED_PROCESS 2
#define WAITING_PROCESS 3

// ---- Valores default para el armado del stack ----
#define FLAG_VALUES 0x202
#define SS_VALUE 0x0				// en nuestro caso se mantiene constante
#define CS_VALUE 0x8

// ------Posiciones para el armado de stack para cada proceso------
										/*		 -=-=STACK=-=-		*/
#define STACK_POINT_OF_ENTRY (21*8)   	/*  	|	RAX, RBX  |		*/
										/*  	|	RCX, etc  |		*/   
#define RDI_POS   (12*8)				/*		---------------		*/ 
#define IP_POS    (6*8)					/*  	|	 RIP	  |		*/				
#define CS_POS 	  (5*8)					/*  	|	  CS	  |		*/
#define FLAGS_POS (4*8)					/*  	|	 RFLAGS	  |		*/
#define SP_POS 	  (3*8)					/*  	|	 RSP	  |		*/
#define SS_POS	  (2*8)					/*  	|	  SS	  |		*/
#define RET_POS 	8 					/*  	|	 RET	  |		*/
										/*   	 -------------		*/

#define STACK_POS(POS) (uint64_t *) (stackStart + STACK_SIZE - (POS))

// -----Informacion sobre cada task-----
typedef struct process_control_block{
		uint64_t  stackPointer;		// valor de rsp 
		uint64_t  stackSegment;  	// valor de ss
		uint8_t screen;				// en que pantalla va a imprimir
		unsigned int pid;				// valor unico identificador
		uint8_t state;				// si el proceso es uno activo o ya se elimino
		uint8_t priority;			// cuantos ticks puede tener por rafaga 
		uint8_t immortal;			// si se puede matar o no
		void * stackStart;
		char ** params;
}process_control_block;

// ------ Queue de tasks -------
static process_control_block tasks[TOTAL_TASKS];

static unsigned int newPidValue = 1;					// identificador para cada proceso
	
static unsigned int currentTask = 0;				// posicion en el array
static unsigned int currentRemainingTicks = 0;			// How many timer ticks are remaining for the current process.
static unsigned int currentDimTasks = NO_TASKS;


/* =========== PROTOYPES =========== */

void idleTask();
void enableMultiTasking();
unsigned int get_current_pid();
uint64_t getRSP();
uint64_t getSS();
uint8_t getCurrentScreen();
int findTask(unsigned int pid);
uint8_t screenAvailable(unsigned int screen);
int add_task(uint64_t entrypoint, uint8_t screen, uint8_t priority, uint8_t immortal, uint64_t arg0);
void pauseScreenProcess(unsigned int screen);
int pauseOrUnpauseProcess(unsigned int pid);
void kill_screen_processes();
void removeCurrentTask();
int removeTask(unsigned int pid);
uint8_t has_or_decrease_time();
uint64_t next_task(uint64_t stackPointer, uint64_t stackSegment);
uint8_t has_children(unsigned int pid);
void wait_for_children(uint64_t rsp, uint64_t ss);
void signal_process_finished(unsigned int pid);
void remove_children(unsigned int fatherPid);
void add_child(unsigned int fatherPid, unsigned int childPid);
unsigned int add_child_task(uint64_t entrypoint, int screen, uint64_t arg0);
uint8_t children_finished(unsigned int fatherPid);


/* =========== CODE =========== */

/* --- Init --- */

void idleTask(){
	while(1)
		_hlt();
}

char * idleArg[] = {"idle", NULL};
void enableMultiTasking(){

	add_task((uint64_t)&idleTask, BACKGROUND, 1, IMMORTAL,idleArg);
	forceCurrentTask();
}


/* --- Getters --- */
unsigned int get_current_pid(){
	return tasks[currentTask].pid;
}
uint64_t getRSP(){
	return tasks[currentTask].stackPointer;
}
uint64_t getSS(){
	return tasks[currentTask].stackSegment;
}
uint8_t getCurrentScreen(){
	return tasks[currentTask].screen;
}
// Encuentro el task usando el pid
int findTask(unsigned int pid){
	for(int i=0; i<TOTAL_TASKS; i++){
		if(tasks[i].pid == pid && tasks[i].state != DEAD_PROCESS)
			return i;
	}	
	return NO_TASK_FOUND;			// no existe task con ese pid
}



/* --- Process Management --- */

int add_task(uint64_t entrypoint, uint8_t screen, uint8_t priority, uint8_t immortal, uint64_t arg0){
	if(currentDimTasks>=TOTAL_TASKS){		// no acepto mas tasks al estar lleno
		return ERROR_NO_SPACE_FOR_TASK;
	}
	currentDimTasks++;

	int pos;
	for(pos=0; tasks[pos].state!=DEAD_PROCESS ;pos++);											// encuentro posicion libre en el array de tasks

	uint8_t * stackStart = mm_malloc(STACK_SIZE);

	if(stackStart == NULL)
		return ERROR_NO_SPACE_FOR_TASK;


	// --- Parametros de funcion ---
	*(STACK_POS(RDI_POS)) = arg0;

	// --- Pongo todos los registros que no se usan en 0 ---
	for(int i=7 ; i<21 ; i++){
		if(i!=12)
			*(STACK_POS(i * 8)) = 0;
	}

	// --- "Stack frame" minimo para la funcion ---

	*(STACK_POS(IP_POS)) = entrypoint;							// puntero al proceso que se va a correr
	*(STACK_POS(CS_POS)) = CS_VALUE;				
	
	*(STACK_POS(FLAGS_POS)) = FLAG_VALUES;						// tenemos que poner el flag de interrupcion en 1 y otros obligatorios
	
	*(STACK_POS(SP_POS)) = (uint64_t) stackStart + STACK_SIZE - RET_POS;	// agarro el comienzo del stack
	*(STACK_POS(SS_POS)) = SS_VALUE;
	
	*(STACK_POS(RET_POS)) = (uint64_t) &removeCurrentTask;		// para el RET que vaya y se remueva automaticamente de los tasks

	// --- Datos de task ---
	tasks[pos].stackPointer = (uint64_t) stackStart + STACK_SIZE - STACK_POINT_OF_ENTRY;					// comienzo del stack
	tasks[pos].stackSegment = SS_VALUE;				// se mantiene constante
	tasks[pos].screen = screen;
	tasks[pos].pid = newPidValue++;
	tasks[pos].state = ACTIVE_PROCESS;
	tasks[pos].priority = priority;
	tasks[pos].immortal = immortal;
	tasks[pos].stackStart = stackStart;
	tasks[pos].params = arg0;

	return tasks[pos].pid;
}


void pauseScreenProcess(unsigned int screen){
	for(int i=0; i<TOTAL_TASKS; i++){
		if(tasks[i].state != WAITING_PROCESS && tasks[i].state != DEAD_PROCESS && tasks[i].screen == screen){
			tasks[i].state = tasks[i].state==PAUSED_PROCESS ? ACTIVE_PROCESS : PAUSED_PROCESS; 	// pausado -> despausado  | despausado -> pausado
		}
	}
}

// pauso o despauso proceso con el pid
int pauseOrUnpauseProcess(unsigned int pid){
	int pos = findTask(pid);
	if(pos < 0)					// se quiere pausar task que no existe
		return NO_TASK_FOUND;

	if(tasks[pos].immortal)
		return -1;

	tasks[pos].state = tasks[pos].state==PAUSED_PROCESS ? ACTIVE_PROCESS : PAUSED_PROCESS; 	// pausado -> despausado  | despausado -> pausado

	return TASK_ALTERED;
}


void kill_screen_processes(){
	for(int i=0; i< TOTAL_TASKS; i++){
		if(tasks[i].state == ACTIVE_PROCESS && tasks[i].immortal != IMMORTAL && tasks[i].screen != BACKGROUND){
			tasks[i].state = DEAD_PROCESS;
			currentDimTasks--;

			signal_process_finished(tasks[i].pid);

			// TODO: quizas sigue corriendo por lo que queda del tick?
		}
	}
}


// params are null terminated array of pointers to strings
void free_params(char ** params){
	if(params==NULL)
		return;

	for(int i=0; params[i]!=NULL; i++){
		mm_free(params[i]);
	}
	mm_free(params);
}

void removeCurrentTask(){
	signal_process_finished(tasks[currentTask].pid);
	mm_free(tasks[currentTask].stackStart);
	free_params(tasks[currentTask].params);

	tasks[currentTask].state = DEAD_PROCESS;
	currentDimTasks--;


	// There's no need to reset currentRemainingTicks, eventually next_task will do so

	forceNextTask(NULL, NULL);				
}

int removeTask(unsigned int pid){

	//TODO: si remuevo el actual entonces tengo que resetear el currentRemainingTicks

	int pos = findTask(pid);
	if(pos < 0)					// se quiere remover task que no existe
		return NO_TASK_FOUND;

	if(tasks[pos].immortal)
		return -1;

	signal_process_finished(pid);
	mm_free(tasks[pos].stackStart);
	free_params(tasks[pos].params);

	tasks[pos].state = DEAD_PROCESS;
	currentDimTasks--;
	return TASK_ALTERED;
}

unsigned int change_priority(unsigned int pid, int delta){
	int pos = findTask(pid);
	if(pos < 0)
		return NO_TASK_FOUND;

	int newPriority = tasks[pos].priority + delta;
	if(newPriority > MAX_PRIORITY)
		newPriority = MAX_PRIORITY;
	else if(newPriority < 1)
		newPriority = DEFAULT_PRIORITY;
	
	tasks[pos].priority = newPriority;

	return 1;
}



/* --- Scheduling --- */

// se fija si le queda tiempo, si le queda, decrementa esa cantidad y
uint8_t has_or_decrease_time(){
	if(currentRemainingTicks < tasks[currentTask].priority - 1){
		currentRemainingTicks++;
		return 1;
	}
	return 0;

}
/*	
	Pasa al proximo task que se tiene que ejecutar. 
	Parametros:  stackPointer: puntero al stack del task anterior  |  stackSegment: valor del stack segment del task anterior  
*/

uint64_t next_task(uint64_t stackPointer, uint64_t stackSegment){

	tasks[currentTask].stackPointer = stackPointer;			// updateo el current
	tasks[currentTask].stackSegment = stackSegment;
	
	char found=0;
	for(unsigned int i=currentTask; !found ; ){			// busco el proximo stack
		i = (i +  1) % TOTAL_TASKS;

		switch(tasks[i].state){

			case ACTIVE_PROCESS:
				currentTask = i;
				found = 1;
				break;

			case WAITING_PROCESS:
				if(children_finished(tasks[i].pid)){
					
					remove_children(tasks[i].pid);

					tasks[i].state = ACTIVE_PROCESS;

					currentTask = i;
					found = 1;
				}
				break;
		}
	}

	currentRemainingTicks = 0;		// reset del current task

	return tasks[currentTask].stackPointer;
}

/* --- Other --- */

void list_process(){
	int len;
	char buffer[100];

	//TODO: RBP????

	writeDispatcher(tasks[currentTask].screen, "Name     |  ID  |  State  |  Prty  |  Stack  |   Rsp   |  Screen\n", 65);
	writeDispatcher(tasks[currentTask].screen, "------------------------------------------------------------------\n", 67);

	for(int i=0; i<TOTAL_TASKS -1 ; i++){
		if(tasks[i].state != DEAD_PROCESS){

			int len = 0;
			if(tasks[i].params !=NULL){
				len = str_len(tasks[i].params[0]);
				writeDispatcher(tasks[currentTask].screen, tasks[i].params[0], len );
			}
			writeDispatcher(tasks[currentTask].screen, "                  ", 12 - len);

			len = num_to_string(tasks[i].pid, buffer);
			writeDispatcher(tasks[currentTask].screen, buffer, len);
			writeDispatcher(tasks[currentTask].screen, "                  ", 5);

			switch(tasks[i].state){
				case ACTIVE_PROCESS: 
					writeDispatcher(tasks[currentTask].screen, "Active ", 7);
					break;
				case PAUSED_PROCESS:
					writeDispatcher(tasks[currentTask].screen, "Paused ", 7);
					break;
				case WAITING_PROCESS:
					writeDispatcher(tasks[currentTask].screen, "Blocked", 7);
					break;
			}
			writeDispatcher(tasks[currentTask].screen, "                  ", 6);


			len = num_to_string(tasks[i].priority, buffer);
			writeDispatcher(tasks[currentTask].screen, buffer, len);
			writeDispatcher(tasks[currentTask].screen, "                  ", 4);


			len = num_to_string(tasks[i].stackStart, buffer);
			writeDispatcher(tasks[currentTask].screen, buffer, len);
			writeDispatcher(tasks[currentTask].screen, "                  ",3);

			len = num_to_string(tasks[i].stackPointer, buffer);
			writeDispatcher(tasks[currentTask].screen, buffer, len);
			writeDispatcher(tasks[currentTask].screen, "                  ",3);

			switch(tasks[i].screen){
				case BACKGROUND:
					writeDispatcher(tasks[currentTask].screen, "Background", 10);
					break;
				case STDOUT:
					writeDispatcher(tasks[currentTask].screen, "Stdout", 6);
					break;
				case STDOUT_LEFT:
					writeDispatcher(tasks[currentTask].screen, "Stdout left", 11);
					break;
				case STDOUT_RIGHT:
					writeDispatcher(tasks[currentTask].screen, "Stdout right", 12);
					break;
			}
			writeDispatcher(tasks[currentTask].screen, "\n", 1);
		}
	}
}


/* --- Child processes --- */

void wait_for_children(uint64_t rsp, uint64_t ss){
	if(!has_children(get_current_pid())){
		return;
	}

	tasks[currentTask].state = WAITING_PROCESS;

	forceNextTask(rsp, ss); 		//	ya tiene en rdi y rsi los parametros para next_task
}


unsigned int add_child_task(uint64_t entrypoint, int screen, uint64_t arg0){
	uint8_t child_pid = add_task(entrypoint, screen, DEFAULT_PRIORITY, MORTAL , arg0);

	add_child(get_current_pid(), child_pid);

	return child_pid;
}


