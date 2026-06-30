// SPDX-License-Identifier: GPL-2.0
/*
 * Burst-Oriented Response Enhancer (BORE) CPU Scheduler
 * Copyright (C) 2021-2024 Masahito Suzuki <firelzrd@gmail.com>
 */

#include "sched.h"
#include <linux/sched/bore.h>

#ifdef CONFIG_SCHED_BORE

uint __read_mostly sched_bore                   = 1;
uint __read_mostly sched_burst_exclude_kthreads = 1;
uint __read_mostly sched_burst_smoothness_long  = 1;
uint __read_mostly sched_burst_smoothness_short = 0;
uint __read_mostly sched_burst_fork_atavistic   = 2;
uint __read_mostly sched_burst_penalty_offset   = 24;
uint __read_mostly sched_burst_penalty_scale    = 1280;
uint __read_mostly sched_burst_cache_stop_count = 64;
uint __read_mostly sched_burst_cache_lifetime   = 60000000;

#define MAX_BURST_PENALTY (39U << 2)

#ifndef task_of
#define task_of(se) container_of(se, struct task_struct, se)
#endif

extern void reweight_task(struct task_struct *p, int prio);

void reset_task_bore(struct task_struct *p)
{
	p->se.burst_time = 0;
	p->se.prev_burst_penalty = 0;
	p->se.curr_burst_penalty = 0;
	p->se.burst_penalty = 0;
	p->se.burst_score = 0;
	p->se.child_burst = 0;
	p->se.child_burst_cnt = 0;
	p->se.child_burst_last_cached = 0;
}

void __init sched_init_bore(void)
{
	reset_task_bore(&init_task);

	printk(KERN_INFO "BORE (Burst-Oriented Response Enhancer) CPU Scheduler modification %s by Masahito Suzuki\n",
	       SCHED_BORE_VERSION);
}

void sched_fork_bore(struct task_struct *p)
{
	reset_task_bore(p);
}

static u32 count_entries_upto2(struct list_head *head)
{
	struct list_head *next = head->next;
	return (next != head) + (next->next != head);
}

static inline bool task_is_bore_eligible(struct task_struct *p)
{
	return p && p->sched_class == &fair_sched_class && !p->exit_state;
}

static inline bool child_burst_cache_expired(struct task_struct *p, u64 now)
{
	u64 expiration_time = p->se.child_burst_last_cached + sched_burst_cache_lifetime;
	return (s64)(expiration_time - now) < 0;
}

static void __update_child_burst_cache(struct task_struct *p, u32 cnt, u32 sum, u64 now)
{
	u8 avg = 0;

	if (cnt)
		avg = sum / cnt;

	p->se.child_burst = max(avg, p->se.burst_penalty);
	p->se.child_burst_cnt = cnt;
	p->se.child_burst_last_cached = now;
}

static inline void update_child_burst_direct(struct task_struct *p, u64 now)
{
	struct task_struct *child;
	u32 cnt = 0;
	u32 sum = 0;

	list_for_each_entry(child, &p->children, sibling) {
		if (!task_is_bore_eligible(child))
			continue;
		cnt++;
		sum += child->se.burst_penalty;
	}

	__update_child_burst_cache(p, cnt, sum, now);
}

static inline u8 __inherit_burst_direct(struct task_struct *p, u64 now)
{
	struct task_struct *parent = p->real_parent;

	if (child_burst_cache_expired(parent, now))
		update_child_burst_direct(parent, now);

	return parent->se.child_burst;
}

static void update_child_burst_topological(struct task_struct *p, u64 now,
					   u32 depth, u32 *acnt, u32 *asum)
{
	struct task_struct *child, *dec;
	u32 cnt = 0, dcnt = 0;
	u32 sum = 0;

	list_for_each_entry(child, &p->children, sibling) {
		dec = child;
		while ((dcnt = count_entries_upto2(&dec->children)) == 1)
			dec = list_first_entry(&dec->children, struct task_struct, sibling);

		if (!dcnt || !depth) {
			if (!task_is_bore_eligible(dec))
				continue;
			cnt++;
			sum += dec->se.burst_penalty;
			continue;
		}
		if (!child_burst_cache_expired(dec, now)) {
			cnt += dec->se.child_burst_cnt;
			sum += (u32)dec->se.child_burst * dec->se.child_burst_cnt;

			if (sched_burst_cache_stop_count <= cnt)
				break;
			
			continue;
		}
		update_child_burst_topological(dec, now, depth - 1, &cnt, &sum);
	}

	__update_child_burst_cache(p, cnt, sum, now);
	*acnt += cnt;
	*asum += sum;
}

static inline u8 __inherit_burst_topological(struct task_struct *p, u64 now)
{
	struct task_struct *anc = p->real_parent;
	u32 cnt = 0, sum = 0;

	while (anc->real_parent != anc && count_entries_upto2(&anc->children) == 1)
		anc = anc->real_parent;

	if (child_burst_cache_expired(anc, now))
		update_child_burst_topological(anc, now,
					       sched_burst_fork_atavistic - 1,
					       &cnt, &sum);

	return anc->se.child_burst;
}

static inline void inherit_burst(struct task_struct *p)
{
	u8 burst_cache;
	u64 now = ktime_get_ns();

	read_lock(&tasklist_lock);
	burst_cache = likely(sched_burst_fork_atavistic) ?
			__inherit_burst_topological(p, now) :
			__inherit_burst_direct(p, now);
	read_unlock(&tasklist_lock);

	p->se.prev_burst_penalty = max(p->se.prev_burst_penalty, burst_cache);
}

void sched_post_fork_bore(struct task_struct *p)
{
	if (p->sched_class == &fair_sched_class)
		inherit_burst(p);
	p->se.burst_penalty = p->se.prev_burst_penalty;
}

static inline u32 log2plus1_u64_u32f8(u64 v)
{
	u32 integral = fls64(v);
	s32 excess_bits = integral - 9;
	u8  fractional = (0 <= excess_bits) ? v >> excess_bits : v << -excess_bits;
	return integral << 8 | fractional;
}

static inline u32 calc_burst_penalty(u64 burst_time)
{
	u32 greed, tolerance, penalty, scaled_penalty;

	greed = log2plus1_u64_u32f8(burst_time);
	tolerance = sched_burst_penalty_offset << 8;
	penalty = max(0, (s32)(greed - tolerance));
	scaled_penalty = penalty * sched_burst_penalty_scale >> 16;

	return min(MAX_BURST_PENALTY, scaled_penalty);
}

static inline u8 effective_prio(struct task_struct *p)
{
	u8 prio = p->static_prio - MAX_RT_PRIO;
	if (likely(sched_bore))
		prio += p->se.burst_score;
	return min(39, prio);
}

void update_burst_score(struct sched_entity *se)
{
	struct task_struct *p;
	u8 prev_prio, new_prio;
	u8 burst_score = 0;

	if (!entity_is_task(se))
		return;

	p = task_of(se);
	prev_prio = effective_prio(p);

	if (!((p->flags & PF_KTHREAD) && likely(sched_burst_exclude_kthreads)))
		burst_score = se->burst_penalty >> 2;
	se->burst_score = burst_score;

	new_prio = effective_prio(p);
	if (new_prio != prev_prio)
		reweight_task(p, new_prio);
}

void update_burst_penalty(struct sched_entity *se)
{
	se->curr_burst_penalty = calc_burst_penalty(se->burst_time);
	se->burst_penalty = max(se->prev_burst_penalty, se->curr_burst_penalty);
	update_burst_score(se);
}

static inline u32 binary_smooth(u32 new, u32 old)
{
	int increment = new - old;

	return (0 <= increment) ?
		old + (increment >> (int)sched_burst_smoothness_long) :
		old - (-increment >> (int)sched_burst_smoothness_short);
}

static void revolve_burst_penalty(struct sched_entity *se)
{
	se->prev_burst_penalty =
		binary_smooth(se->curr_burst_penalty, se->prev_burst_penalty);
	se->curr_burst_penalty = 0;
	se->burst_time = 0;
}

void restart_burst(struct sched_entity *se)
{
	revolve_burst_penalty(se);
	se->burst_penalty = se->prev_burst_penalty;
	update_burst_score(se);
}
#endif // CONFIG_SCHED_BORE
