/* $Id: process.c,v 1.28 2000/03/05 02:16:15 gniibe Exp $
 *
 *  linux/arch/sh/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  SuperH version:  Copyright (C) 1999, 2000  Niibe Yutaka & Kaz Kojima
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/elf.h>

#include <linux/irq.h>

static int hlt_counter=0;

#define HARD_IDLE_TIMEOUT (HZ / 3)

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

/*
 * The idle loop on a uniprocessor i386..
 */ 
void cpu_idle(void *unused)
{
	/* endless idle loop with no priority at all */
	init_idle();
	current->priority = 0;
	current->counter = -100;

	while (1) {
		while (!current->need_resched) {
			if (hlt_counter)
				continue;
			__sti();
			asm volatile("sleep" : : : "memory");
		}
		schedule();
		check_pgt_cache();
	}
}

void machine_restart(char * __unused)
{ /* Need to set MMU_TTB?? */
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("PC  : %08lx SP  : %08lx SR  : %08lx TEA : %08lx\n",
	       regs->pc, regs->sp, regs->sr, ctrl_inl(MMU_TEA));
	printk("R0  : %08lx R1  : %08lx R2  : %08lx R3  : %08lx\n",
	       regs->regs[0],regs->regs[1],
	       regs->regs[2],regs->regs[3]);
	printk("R4  : %08lx R5  : %08lx R6  : %08lx R7  : %08lx\n",
	       regs->regs[4],regs->regs[5],
	       regs->regs[6],regs->regs[7]);
	printk("R8  : %08lx R9  : %08lx R10 : %08lx R11 : %08lx\n",
	       regs->regs[8],regs->regs[9],
	       regs->regs[10],regs->regs[11]);
	printk("R12 : %08lx R13 : %08lx R14 : %08lx\n",
	       regs->regs[12],regs->regs[13],
	       regs->regs[14]);
	printk("MACH: %08lx MACL: %08lx GBR : %08lx PR  : %08lx\n",
	       regs->mach, regs->macl, regs->gbr, regs->pr);
}

struct task_struct * alloc_task_struct(void)
{
	/* Get two pages */
	return (struct task_struct *) __get_free_pages(GFP_KERNEL,1);
}

void free_task_struct(struct task_struct *p)
{
	free_pages((unsigned long) p, 1);
}

/*
 * Create a kernel thread
 */

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process(ie the swapper or direct descendants
 * who haven't done an "execve()") should use this: it will work within
 * a system call from a "real" process, but the process memory space will
 * not be free'd until both the parent and the child have exited.
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{	/* Don't use this in BL=1(cli).  Or else, CPU resets! */
	register unsigned long __sc0 __asm__ ("$r0") = __NR_clone;
	register unsigned long __sc4 __asm__ ("$r4") = (long) flags | CLONE_VM;
	register unsigned long __sc5 __asm__ ("$r5") = 0;
	register unsigned long __sc8 __asm__ ("$r8") = (long) arg;
	register unsigned long __sc9 __asm__ ("$r9") = (long) fn;

	__asm__("trapa	#0\n\t" 	/* Linux/SH system call */
		"tst	#0xff, $r0\n\t"	/* child or parent? */
		"bf	1f\n\t"		/* parent - jump */
		"jsr	@$r9\n\t"	/* call fn */
		" mov	$r8, $r4\n\t"	/* push argument */
		"mov	$r0, $r4\n\t"	/* return value to arg of exit */
		"mov	%2, $r0\n\t"	/* exit */
		"trapa	#0\n"
		"1:"
		: "=z" (__sc0)
		: "0" (__sc0), "i" (__NR_exit),
		  "r" (__sc4), "r" (__sc5), "r" (__sc8), "r" (__sc9)
		: "memory");
	return __sc0;
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/* Nothing to do. */
}

void flush_thread(void)
{
#if defined(__sh3__)
	/* do nothing */
	/* Possibly, set clear debug registers */
#elif defined(__SH4__)
	struct task_struct *tsk = current;

	/* Forget lazy FPU state */
	clear_fpu(tsk);
	tsk->used_math = 0;
#endif
}

void release_thread(struct task_struct *dead_task)
{
	/* do nothing */
}

/* Fill in the fpu structure for a core dump.. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
#if defined(__SH4__)
	int fpvalid;
	struct task_struct *tsk = current;

	fpvalid = tsk->used_math;
	if (fpvalid) {
		unlazy_fpu(tsk);
		memcpy(fpu, &tsk->thread.fpu.hard, sizeof(*fpu));
	}

	return fpvalid;
#else
	return 0; /* Task didn't use the fpu at all. */
#endif
}

asmlinkage void ret_from_fork(void);

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
	struct task_struct *tsk = current;

	childregs = ((struct pt_regs *)(THREAD_SIZE + (unsigned long) p)) - 1;
	struct_cpy(childregs, regs);

#if defined(__SH4__)
	if (tsk != &init_task) {
		unlazy_fpu(tsk);
		struct_cpy(&p->thread.fpu, &current->thread.fpu);
		p->used_math = tsk->used_math;
	}
#endif
	if (user_mode(regs)) {
		childregs->sp = usp;
	} else {
		childregs->sp = (unsigned long)p+2*PAGE_SIZE;
	}
	childregs->regs[0] = 0; /* Set return value for child */
	childregs->sr |= SR_FD; /* Invalidate FPU flag */

	p->thread.sp = (unsigned long) childregs;
	p->thread.pc = (unsigned long) ret_from_fork;

	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	dump->magic = CMAGIC;
	dump->start_code = current->mm->start_code;
	dump->start_data  = current->mm->start_data;
	dump->start_stack = regs->sp & ~(PAGE_SIZE - 1);
	dump->u_tsize = (current->mm->end_code - dump->start_code) >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk + (PAGE_SIZE-1) - dump->start_data) >> PAGE_SHIFT;
	dump->u_ssize = (current->mm->start_stack - dump->start_stack +
			 PAGE_SIZE - 1) >> PAGE_SHIFT;
	/* Debug registers will come here. */

	dump->regs = *regs;

	dump->u_fpvalid = dump_fpu(regs, &dump->fpu);
}

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 */
void __switch_to(struct task_struct *prev, struct task_struct *next)
{
#if defined(__SH4__)
	if (prev != &init_task)
		unlazy_fpu(prev);
#endif
	/*
	 * Restore the kernel stack onto kernel mode register
	 *   	k4 (r4_bank1)
	 */
	asm volatile("ldc	%0, $r4_bank"
		     : /* no output */
		     :"r" ((unsigned long)next+8192));
}

asmlinkage int sys_fork(unsigned long r4, unsigned long r5,
			unsigned long r6, unsigned long r7,
			struct pt_regs regs)
{
	return do_fork(SIGCHLD, regs.sp, &regs);
}

asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
			 unsigned long r6, unsigned long r7,
			 struct pt_regs regs)
{
	if (!newsp)
		newsp = regs.sp;
	return do_fork(clone_flags, newsp, &regs);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage int sys_vfork(unsigned long r4, unsigned long r5,
			 unsigned long r6, unsigned long r7,
			 struct pt_regs regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.sp, &regs);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(char *ufilename, char **uargv,
			  char **uenvp, unsigned long r7,
			  struct pt_regs regs)
{
	int error;
	char *filename;

	lock_kernel();
	filename = getname(ufilename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	error = do_execve(filename, uargv, uenvp, &regs);
	if (error == 0)
		current->flags &= ~PF_DTRACE;
	putname(filename);
out:
	unlock_kernel();
	return error;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long schedule_frame;
	unsigned long pc;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	/*
	 * The same comment as on the Alpha applies here, too ...
	 */
	pc = thread_saved_pc(&p->thread);
	if (pc >= (unsigned long) interruptible_sleep_on && pc < (unsigned long) add_timer) {
		schedule_frame = ((unsigned long *)(long)p->thread.sp)[1];
		return (unsigned long)((unsigned long *)schedule_frame)[1];
	}
	return pc;
}

asmlinkage void print_syscall(int x)
{
	unsigned long flags, sr;
	asm("stc	$sr, %0": "=r" (sr));
	save_and_cli(flags);
	printk("%c: %c %c, %c: SYSCALL\n", (x&63)+32,
	       (current->flags&PF_USEDFPU)?'C':' ',
	       (init_task.flags&PF_USEDFPU)?'K':' ', (sr&SR_FD)?' ':'F');
	restore_flags(flags);
}
