#ifndef _LINUX_SCHED_BORE_H
#define _LINUX_SCHED_BORE_H

#include <linux/sched.h>

#define SCHED_BORE_VERSION "5.1.0+"

#ifdef CONFIG_SCHED_BORE

extern uint __read_mostly sched_bore;
extern uint __read_mostly sched_burst_exclude_kthreads;
extern uint __read_mostly sched_burst_smoothness_long;
extern uint __read_mostly sched_burst_smoothness_short;
extern uint __read_mostly sched_burst_fork_atavistic;
extern uint __read_mostly sched_burst_penalty_offset;
extern uint __read_mostly sched_burst_penalty_scale;
extern uint __read_mostly sched_burst_cache_stop_count;
extern uint __read_mostly sched_burst_cache_lifetime;

extern void __init sched_init_bore(void);
extern void sched_fork_bore(struct task_struct *p);
extern void sched_post_fork_bore(struct task_struct *p);
extern void reset_task_bore(struct task_struct *p);

extern void update_burst_score(struct sched_entity *se);
extern void update_burst_penalty(struct sched_entity *se);
extern void restart_burst(struct sched_entity *se);

#endif /* CONFIG_SCHED_BORE */
#endif /* _LINUX_SCHED_BORE_H */
