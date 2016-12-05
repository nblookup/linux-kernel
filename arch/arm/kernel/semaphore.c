/*
 * ARM semaphore implementation, taken from
 *
 * i386 semaphore implementation.
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Modified for ARM by Russell King
 */
#include <linux/sched.h>
#include <linux/errno.h>

#include <asm/semaphore.h>

/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to aquire the semaphore, while the "sleeping"
 * variable is a count of such aquires.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * "sleeping" and the contention routine ordering is
 * protected by the semaphore spinlock.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */

/*
 * Logic:
 *  - only on a boundary condition do we need to care. When we go
 *    from a negative count to a non-negative, we wake people up.
 *  - when we go from a non-negative count to a negative do we
 *    (a) synchronize with the "sleeper" count and (b) make sure
 *    that we're on the wakeup list before we synchronize so that
 *    we cannot lose wakeup events.
 */

void __up(struct semaphore *sem)
{
	wake_up(&sem->wait);
}

static spinlock_t semaphore_lock = SPIN_LOCK_UNLOCKED;

void __down(struct semaphore * sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	tsk->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue(&sem->wait, &wait);

	spin_lock_irq(&semaphore_lock);
	sem->sleepers++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			wake_up(&sem->wait);
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irq(&semaphore_lock);

		schedule();
		tsk->state = TASK_UNINTERRUPTIBLE;
		spin_lock_irq(&semaphore_lock);
	}
	spin_unlock_irq(&semaphore_lock);
	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}

int __down_interruptible(struct semaphore * sem)
{
	int retval;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	tsk->state = TASK_INTERRUPTIBLE;
	add_wait_queue(&sem->wait, &wait);

	spin_lock_irq(&semaphore_lock);
	sem->sleepers ++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * With signals pending, this turns into
		 * the trylock failure case - we won't be
		 * sleeping, and we* can't get the lock as
		 * it has contention. Just correct the count
		 * and exit.
		 */
		retval = -EINTR;
		if (signal_pending(current)) {
			sem->sleepers = 0;
			if (atomic_add_negative(sleepers, &sem->count))
				break;
			wake_up(&sem->wait);
			break;
		}

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock. The
		 * "-1" is because we're still hoping to get
		 * the lock.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			wake_up(&sem->wait);
			retval = 0;
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irq(&semaphore_lock);

		schedule();
		tsk->state = TASK_INTERRUPTIBLE;
		spin_lock_irq(&semaphore_lock);
	}
	spin_unlock_irq(&semaphore_lock);
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&sem->wait, &wait);
	return retval;
}

/*
 * Trylock failed - make sure we correct for
 * having decremented the count.
 *
 * We could have done the trylock with a
 * single "cmpxchg" without failure cases,
 * but then it wouldn't work on a 386.
 */
int __down_trylock(struct semaphore * sem)
{
	int sleepers;

	spin_lock_irq(&semaphore_lock);
	sleepers = sem->sleepers + 1;
	sem->sleepers = 0;

	/*
	 * Add "everybody else" and us into it. They aren't
	 * playing, because we own the spinlock.
	 */
	if (!atomic_add_negative(sleepers, &sem->count))
		wake_up(&sem->wait);

	spin_unlock_irq(&semaphore_lock);
	return 1;
}

struct rw_semaphore *down_read_failed_biased(struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	add_wait_queue(&sem->wait, &wait);	/* put ourselves at the head of the list */

	for (;;) {
		if (sem->read_bias_granted && xchg(&sem->read_bias_granted, 0))
			break;
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (!sem->read_bias_granted)
			schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;

	return sem;
}

struct rw_semaphore *down_write_failed_biased(struct rw_semaphore *sem)
{
        struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	add_wait_queue_exclusive(&sem->write_bias_wait, &wait); /* put ourselves at the end of the list */

	for (;;) {
		if (sem->write_bias_granted && xchg(&sem->write_bias_granted, 0))
			break;
		set_task_state(tsk, TASK_UNINTERRUPTIBLE | TASK_EXCLUSIVE);
		if (!sem->write_bias_granted)
			schedule();
	}

	remove_wait_queue(&sem->write_bias_wait, &wait);
	tsk->state = TASK_RUNNING;

	/* if the lock is currently unbiased, awaken the sleepers
	 * FIXME: this wakes up the readers early in a bit of a
	 * stampede -> bad!
	 */
	if (atomic_read(&sem->count) >= 0)
		wake_up(&sem->wait);

	return sem;
}

/* Wait for the lock to become unbiased.  Readers
 * are non-exclusive. =)
 */
struct rw_semaphore *down_read_failed(struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	/* this takes care of granting the lock */
	__up_op_read(sem, __rwsem_wake);

	add_wait_queue(&sem->wait, &wait);

	while (atomic_read(&sem->count) < 0) {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&sem->count) >= 0)
			break;
		schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;

	return sem;
}

/* Wait for the lock to become unbiased. Since we're
 * a writer, we'll make ourselves exclusive.
 */
struct rw_semaphore *down_write_failed(struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	/* this takes care of granting the lock */
	__up_op_write(sem, __rwsem_wake);

	add_wait_queue_exclusive(&sem->wait, &wait);

	while (atomic_read(&sem->count) < 0) {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE | TASK_EXCLUSIVE);
		if (atomic_read(&sem->count) >= 0)
			break;	/* we must attempt to aquire or bias the lock */		schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;

	return sem;
}

/* Called when someone has done an up that transitioned from
 * negative to non-negative, meaning that the lock has been
 * granted to whomever owned the bias.
 */
struct rw_semaphore *rwsem_wake_readers(struct rw_semaphore *sem)
{
	if (xchg(&sem->read_bias_granted, 1))
		BUG();
	wake_up(&sem->wait);
	return sem;
}

struct rw_semaphore *rwsem_wake_writer(struct rw_semaphore *sem)
{
	if (xchg(&sem->write_bias_granted, 1))
		BUG();
	wake_up(&sem->write_bias_wait);
	return sem;
}

/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 *
 * r0 contains the semaphore pointer on entry. Save the C-clobbered
 * registers (r0 to r3, ip and lr) except r0 in the cases where it
 * is used as a return value..
 */
asm("	.section	.text.lock, \"ax\"
	.align	5
	.globl	__down_failed
__down_failed:
	stmfd	sp!, {r0 - r3, ip, lr}
	bl	__down
	ldmfd	sp!, {r0 - r3, ip, pc}

	.align	5
	.globl	__down_interruptible_failed
__down_interruptible_failed:
	stmfd	sp!, {r1 - r3, ip, lr}
	bl	__down_interruptible
	ldmfd	sp!, {r1 - r3, ip, pc}

	.align	5
	.globl	__down_trylock_failed
__down_trylock_failed:
	stmfd	sp!, {r1 - r3, ip, lr}
	bl	__down_trylock
	ldmfd	sp!, {r1 - r3, ip, pc}

	.align	5
	.globl	__up_wakeup
__up_wakeup:
	stmfd	sp!, {r0 - r3, ip, lr}
	bl	__up
	ldmfd	sp!, {r0 - r3, ip, pc}

	.align	5
	.globl	__down_read_failed
__down_read_failed:
	stmfd	sp!, {r0 - r3, ip, lr}
	bcc	1f
	bl	down_read_failed_biased
	ldmfd	sp!, {r0 - r3, ip, pc}
1:	bl	down_read_failed
	/***/

	.align	5
	.globl	__down_write_failed
__down_write_failed:
	stmfd	sp!, {r0 - r3, ip, lr}
	bcc	1f
	bl	down_write_failed_biased
	ldmfd	sp!, {r0 - r3, ip, pc}
1:	bl	down_write_failed
	/***/

	.align	5
	.globl	__rwsem_wake
__rwsem_wake:
	stmfd	sp!, {r0 - r3, ip, lr}
	beq	1f
	bl	rwsem_wake_readers
	ldmfd	sp!, {r0 - r3, ip, pc}
1:	bl	rwsem_wake_writer
	ldmfd	sp!, {r0 - r3, ip, pc}

	.previous
	");

