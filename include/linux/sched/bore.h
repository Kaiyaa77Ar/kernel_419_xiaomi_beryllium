/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Burst-Oriented Response Enhancer (BORE) CPU Scheduler
 * Copyright (C) 2021-2024 Masahito Suzuki <firelzrd@gmail.com>
 */

#include <linux/sched.h>

#ifndef _LINUX_SCHED_BORE_H
#define _LINUX_SCHED_BORE_H

#define SCHED_BORE_VERSION "5.1.0"

#ifdef CONFIG_SCHED_BORE

extern uint __read_mostly sched_bore;
extern uint __read_mostly sched_burst_smoothness_long;
extern uint __read_mostly sched_burst_smoothness_short;
extern uint __read_mostly sched_burst_fork_atavistic;
extern uint __read_mostly sched_burst_penalty_offset;
extern uint __read_mostly sched_burst_penalty_scale;
extern uint __read_mostly sched_burst_cache_lifetime;

extern void __init sched_init_bore(void);
extern void sched_fork_bore(struct task_struct *p);
extern void sched_post_fork_bore(struct task_struct *p);

extern void update_burst_score(struct sched_entity *se);
extern void update_burst_penalty(struct sched_entity *se);
extern void restart_burst(struct sched_entity *se);

#endif /* CONFIG_SCHED_BORE */
#endif /* _LINUX_SCHED_BORE_H */
