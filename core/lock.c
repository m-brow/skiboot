/* Copyright 2013-2014 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <skiboot.h>
#include <lock.h>
#include <assert.h>
#include <processor.h>
#include <cpu.h>
#include <console.h>
#include <timebase.h>

/* Set to bust locks. Note, this is initialized to true because our
 * lock debugging code is not going to work until we have the per
 * CPU data initialized
 */
bool bust_locks = true;

#ifdef DEBUG_LOCKS
static struct lock dl_lock = LOCK_UNLOCKED;

static void lock_error(struct lock *l, const char *reason, uint16_t err)
{
	bust_locks = true;

	fprintf(stderr, "LOCK ERROR: %s @%p (state: 0x%016lx)\n",
		reason, l, l->lock_val);
	op_display(OP_FATAL, OP_MOD_LOCK, err);

	abort();
}

static void lock_check(struct lock *l)
{
	if ((l->lock_val & 1) && (l->lock_val >> 32) == this_cpu()->pir)
		lock_error(l, "Invalid recursive lock", 0);
}

static void unlock_check(struct lock *l)
{
	if (!(l->lock_val & 1))
		lock_error(l, "Unlocking unlocked lock", 1);

	if ((l->lock_val >> 32) != this_cpu()->pir)
		lock_error(l, "Unlocked non-owned lock", 2);

	if (l->in_con_path && this_cpu()->con_suspend == 0)
		lock_error(l, "Unlock con lock with console not suspended", 3);

	if (this_cpu()->lock_depth == 0)
		lock_error(l, "Releasing lock with 0 depth", 4);
}

/* Find circular dependencies in the lock requests. */
static bool check_deadlock(void)
{
	uint32_t lock_owner, start, i;
	struct cpu_thread *next_cpu;
	struct lock *next;

	next  = this_cpu()->requested_lock;
	start = this_cpu()->pir;
	i = 0;

	while (i < cpu_max_pir) {

		if (!next)
			return false;

		if (!(next->lock_val & 1) || next->in_con_path)
			return false;

		lock_owner = next->lock_val >> 32;

		if (lock_owner == start)
			return true;

		next_cpu = find_cpu_by_pir(lock_owner);

		if (!next_cpu)
			return false;

		next = next_cpu->requested_lock;
		i++;
	}

	return false;
}

static void add_lock_request(struct lock *l)
{
	struct cpu_thread *curr = this_cpu();

	if (curr->state != cpu_state_active &&
	    curr->state != cpu_state_os)
		return;

	/*
	 * For deadlock detection we must keep the lock states constant
	 * while doing the deadlock check.
	 */
	for (;;) {
		if (try_lock(&dl_lock))
			break;
		smt_lowest();
		while (dl_lock.lock_val)
			barrier();
		smt_medium();
	}

	curr->requested_lock = l;

	if (check_deadlock())
		lock_error(l, "Deadlock detected", 0);

	unlock(&dl_lock);
}

static void remove_lock_request(void)
{
	this_cpu()->requested_lock = NULL;
}

#define LOCK_TIMEOUT_MS 10000
static inline bool lock_timeout(unsigned long start)
{
	/* Print warning if lock has been spinning for more than TIMEOUT_MS */
	unsigned long wait = tb_to_msecs(mftb());

	if (wait - start > LOCK_TIMEOUT_MS) {
		prlog(PR_WARNING, "WARNING: Lock has been "\
		      "spinning for %lums\n", wait - start);
		backtrace();
		return true;
	}

	return false;
}
#else
static inline void lock_check(struct lock *l) { };
static inline void unlock_check(struct lock *l) { };
static inline void add_lock_request(struct lock *l) { };
static inline void remove_lock_request(void) { };
static inline bool lock_timeout(unsigned long s) { return false; }
#endif /* DEBUG_LOCKS */

bool lock_held_by_me(struct lock *l)
{
	uint64_t pir64 = this_cpu()->pir;

	return l->lock_val == ((pir64 << 32) | 1);
}

bool try_lock(struct lock *l)
{
	if (__try_lock(l)) {
		if (l->in_con_path)
			this_cpu()->con_suspend++;
		this_cpu()->lock_depth++;
		return true;
	}
	return false;
}


void lock(struct lock *l)
{
	bool timeout_warn = false;
	unsigned long start;

	if (bust_locks)
		return;

	lock_check(l);

	if (try_lock(l))
		return;
	add_lock_request(l);

#ifdef DEBUG_LOCKS
	start = tb_to_msecs(mftb());
#endif

	for (;;) {
		if (try_lock(l))
			break;
		smt_lowest();
		while (l->lock_val)
			barrier();
		smt_medium();

		if (!timeout_warn)
			timeout_warn = lock_timeout(start);
	}

	remove_lock_request();
}

void unlock(struct lock *l)
{
	struct cpu_thread *cpu = this_cpu();

	if (bust_locks)
		return;

	unlock_check(l);

	lwsync();
	this_cpu()->lock_depth--;
	l->lock_val = 0;

	/* WARNING: On fast reboot, we can be reset right at that
	 * point, so the reset_lock in there cannot be in the con path
	 */
	if (l->in_con_path) {
		cpu->con_suspend--;
		if (cpu->con_suspend == 0 && cpu->con_need_flush)
			flush_console();
	}
}

bool lock_recursive(struct lock *l)
{
	if (bust_locks)
		return false;

	if (lock_held_by_me(l))
		return false;

	lock(l);
	return true;
}

void init_locks(void)
{
	bust_locks = false;
}
