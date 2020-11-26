// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "mon_backtrace", "Display information about the stack", mon_backtrace},
	{ "showva2pa", "Display the physical address of the virtual address, max argument 2", showva2pa}
	// { "backtrace", "Display debug information about the stack", backtrace}
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t *ebp = (uint32_t*) read_ebp();
	cprintf("Stack backtrace:\n");
	while (ebp) {
		uintptr_t eip = ebp[1];
		cprintf("  ebp %08x  eip %08x  args", ebp, eip);
		for (int i = 2; i <= 6; ++i)
			cprintf(" %08x", ebp[i]);
		cprintf("\n");

		struct Eipdebuginfo info;
		debuginfo_eip(eip, &info);
		cprintf("         %s:%d: %.*s+%d\n",
		  info.eip_file, info.eip_line, info.eip_fn_namelen, 
		  info.eip_fn_name, eip - info.eip_fn_addr);
		ebp = (uint32_t*) *ebp;
	}
	return 0;
}

int
showva2pa(int argc, char **argv, struct Trapframe *tf){
	// cprintf("argc: %d", argc);
	if(argc > 3 || argc < 2){
		// cprintf("argc: %d", argc);
		cprintf("Illegal arguments number (max: 2, min: 1)!\n");
		return 0;
	}
	
	uintptr_t VirtualAddress[2];
	for(int i = 1, len = 0; i < argc; ++i){
		len = strlen(argv[i]);
		if( (argv[i][0] != '0' && argv[i][1] != 'x') || len != 10){
			cprintf("Illegal arguments: %s!\n", argv[i]);
			return 0;
		}
		VirtualAddress[i-1] = H2D(argv[i] + 2);
	}
	if(argc == 2){
		showva2pa_print((void *)VirtualAddress[0]);
		return 0;
	}
	for(uintptr_t va = VirtualAddress[0]; va <= VirtualAddress[1]; va += PGSIZE)
		showva2pa_print((void *)va);
	return 0;
}

void showva2pa_print(void *va){
    pte_t *pte_p = pgdir_walk(kern_pgdir, va, false);
    struct PageInfo *pginfo;
	bool pte_w, pte_u;
    cprintf("VA: 0x%08x, ", (physaddr_t)va);
    if (!pte_p || !(*pte_p & PTE_P)){
        cprintf("doesn't have a mapped pysical page!\n");
        return;
    }
    pginfo = pa2page(PTE_ADDR(*(pte_p)));
	if( *(pte_p) | PTE_W)
		pte_w = true;
	if(*(pte_p) | PTE_U)
		pte_u = true;
    cprintf("PA: 0x%08x, pp_ref: %3d, PTE_W: %1d, PTE_U: %1d\n", PTE_ADDR(*(pte_p)),
        pginfo->pp_ref, pte_w, pte_u);
	return;
}

unsigned int H2D(char *s){
	int len = strlen(s);
	char c;
	uint32_t result = 0;
	for(int i = 0; i < len; ++i){
		result *= 16;
		c = s[i];
		if(c <= '9' && c >= '0')
			result += c - '0';
		else if(c >= 'a' && c <= 'f')
			result += c - 'a' + 10;
		else return -1;
	}
	return result;
}

// int
// backtrace(int argc, char **argv, struct Trapframe *tf)
// {
// 	uint32_t *ebp = (uint32_t*) read_ebp();
// 	cprintf("Stack backtrace:\n");
// 	while (ebp) {
// 		uintptr_t eip = ebp[1];
// 		cprintf("  ebp %08x  eip %08x  args", ebp, eip);
// 		for (int i = 2; i <= 6; ++i)
// 			cprintf(" %08x", ebp[i]);
// 		cprintf("\n");

// 		struct Eipdebuginfo info;
// 		debuginfo_eip(eip, &info);
// 		cprintf("         %s:%d: %.*s+%d\n",
// 		  info.eip_file, info.eip_line, info.eip_fn_namelen, 
// 		  info.eip_fn_name, eip - info.eip_fn_addr);
// 		ebp = (uint32_t*) *ebp;
// 	}
// 	return 0;
// }



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
