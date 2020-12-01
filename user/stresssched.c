#include <inc/lib.h>

volatile int counter;

void
umain(int argc, char **argv)
{
	int i, j;
	int seen;
	envid_t parent = sys_getenvid();

	// Fork several environments
	for (i = 0; i < 20; i++)
		if (fork() == 0)
			break;
	if (i == 20) {
		sys_yield();
		return;
	}

	// cprintf("1. Process %d\n", sys_getenvid());

	// Wait for the parent to finish forking
	while (envs[ENVX(parent)].env_status != ENV_FREE)
		asm volatile("pause");

	// cprintf("2. Process %d\n", sys_getenvid());
	// Check that one environment doesn't run on two CPUs at once
	for (i = 0; i < 10; i++) {
		sys_yield();
		for (j = 0; j < 10000; j++)
			counter++;
	}

	// cprintf("3. Process %d\n", sys_getenvid());
	if (counter != 10*10000)
		panic("ran on two CPUs at once (counter is %d)", counter);

	// Check that we see environments running on different CPUs
	cprintf("[%08x] stresssched on CPU %d\n", thisenv->env_id, thisenv->env_cpunum);

}

