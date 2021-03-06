/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <spl.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(*lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);
	
	//the inital state of the holding thread must be NULL
	//because its value is used to check if the lock is 
	//available
	lock->holding_thread = NULL;
	
	//initialize the lock's internal spinlock
	spinlock_init(&lock->lock_lock);
	
	//create the waiting channel for the lock
	lock->lock_wchan = wchan_create(lock->lk_name);
	
	//if the waiting channel is unable to be created
	//the function will deallocate the lock and return NULL 
	if(lock->lock_wchan==NULL){
     	spinlock_cleanup(&lock->lock_lock);
	kfree(lock->lk_name);
     	kfree(lock);
     	return NULL;
	}
	
	return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);
	//Deallocate the spinlock, waiting channel, the
	//lock and its name
	spinlock_cleanup(&lock->lock_lock);
	wchan_destroy(lock->lock_wchan);
        kfree(lock->lk_name);
        kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
        //Ensure that the lock being passed in exists
	KASSERT(lock != NULL);
        
	//Check that the calling thread is not being interrupted
	KASSERT(curthread->t_in_interrupt == false);
	
	//Ensure that the calling thread does not already hold the lock
	KASSERT(!lock_do_i_hold(lock));
	

	//surround the lock-aquire code with a spinlock to avoid
	//multiple threads trying to access the lock at once
	spinlock_acquire(&lock->lock_lock);

	//Make the following section atomic by setting the Priority
	//Level to its highest
	int spl = splhigh();
	
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);


        // When a thread reaches this point, if there is another thread holding the lock
        // (the lock's holding_thread pointer is not NULL) then they will be added to the
        // waiting channel.
	while(lock->holding_thread != NULL)
	{
	wchan_sleep(lock->lock_wchan, &lock->lock_lock);
	}
	
	//the current thread will aquire the lock
	lock->holding_thread = curthread;
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
	

	//Restore the Priority Level to the level it was at before the 
	//call
	splx(spl);
	
	spinlock_release(&lock->lock_lock);

}
void
lock_release(struct lock *lock)
{

        //Ensure that the lock being passed in exists
	KASSERT(lock != NULL);
	
	//ensure that the calling thread has the lock
	KASSERT(lock->holding_thread == curthread);

	//use a spinlock to ensure multiple threads do not try to release the
	//lock at the same time (potentially waking up multiple threads)
	spinlock_acquire(&lock->lock_lock);
	

	//Make the following section atomic by setting the Priority
        //Level to its highest
	int spl = splhigh();

	//The lock is released
	lock->holding_thread = NULL;

	//release a thread from the waiting channel
	wchan_wakeone(lock->lock_wchan, &lock->lock_lock);

	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);
        
	//Restore the Priority Level to the level it was at before the 
        //call
	splx(spl);
	
	spinlock_release(&lock->lock_lock);
		
}

bool
lock_do_i_hold(struct lock *lock)
{
	//returns true when the calling thread 
	//holds the lock
        KASSERT(lock != NULL);
	
	return (lock->holding_thread == curthread);
}


////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

	// add stuff here as needed

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	// add stuff here as needed

	kfree(cv->cv_name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Write this
	(void)cv;    // suppress warning until code gets written
	(void)lock;  // suppress warning until code gets written
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	// Write this
	(void)cv;    // suppress warning until code gets written
	(void)lock;  // suppress warning until code gets written
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Write this
	(void)cv;    // suppress warning until code gets written
	(void)lock;  // suppress warning until code gets written
}
