// yield the processor to other environments

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	// int i;

	// cprintf("Hello, I am environment %08x.\n", thisenv->env_id);
	// for (i = 0; i < 5; i++) {
	// 	sys_yield();
	// 	cprintf("Back in environment %08x, iteration %d.\n",
	// 		thisenv->env_id, i);
	// }
	// cprintf("All done in environment %08x.\n", thisenv->env_id);

	int i;
    sys_env_set_priority(1);

    for(int i = 0; i < 2; ++i){
		if(fork() == 0){
			 sys_env_set_priority(i+1);
			break;
		}
	}

	cprintf("Hello, I am environment %08x. Priority %d\n", thisenv->env_id, thisenv->env_priority);
	for (i = 0; i < 5; i++) {
		sys_yield();
		cprintf("Back in environment %08x, Priority %d iteration %d.\n",
			thisenv->env_id, thisenv->env_priority, i);
	}
	cprintf("All done in environment %08x, Priority %d.\n", thisenv->env_id, thisenv->env_priority, thisenv->env_priority);
}
