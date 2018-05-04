#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include <linux/version.h>

#ifdef TKCORE_BL
#include <linux/kthread.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <asm/topology.h>
#endif

#include <linux/tee_clkmgr.h>

#include <tee_kernel_lowlevel_api.h>
#include <arm_common/teesmc.h>

#ifdef TKCORE_BL

static int nr_cpus __read_mostly;

/* if a smc_xfer waits over 2ms
   for one available core, we give up
   and choose any core that is available
   at this moment of time */
static const s64 smc_xfer_timeout_us = 2000LL;

typedef struct {
	struct smc_param *param;
	struct list_head entry;

	struct semaphore done_sem;
} smc_xfer_cmd_t;

typedef struct {
	struct task_struct *thread;
	wait_queue_head_t thread_wq;
} smc_xfer_thread_t;

static bool cpu_is_big(int cpu);

#endif

typedef struct {
	/* number of cmds that are not
	   processed by `tee_smc_daemon` kthreads */
	atomic_t nr_unbound_cmds;
	/* guarantee the mutual-exlusiveness of smc */
	struct mutex xfer_lock;

	/* cmds that wait for
	   available TEE thread slots */
	atomic_t nr_waiting_cmds;
	struct semaphore xfer_wait_queue;

#ifdef TKCORE_BL
	/* whether the platform is actually
	   a big-little platform */
	bool platform_is_big_little;

	/* The candidate big cpu is the cpu that will be
		started if no big cpu is online*/
	int candidate_big_cpu;

	/* records information for the big cpus that are locked
	   in the online state due to running smc commands */
	uint32_t cpu_lock_mask;

	struct list_head cmd_head;
	spinlock_t cmd_lock;

	struct notifier_block tee_cpu_notifier;

	/* tee_smc_daemon */
	bool xfer_thread_running;

	smc_xfer_thread_t *xfer_threads;
	int xfer_threads_count;
	int cpuid_to_tid[NR_CPUS];
#endif

	/* statistics information */
	s64 max_smc_time;
	s64 max_cmd_time;

} tee_smc_xfer_ctl_t;

static tee_smc_xfer_ctl_t tee_smc_xfer_ctl;

static inline void trace_tee_smc(tee_smc_xfer_ctl_t *ctl, int rv, s64 time_start, s64 time_end)
{
	s64 duration = time_end - time_start;

	if (duration > 1000000LL) {
		pr_warn("WARNING SMC[0x%x] %sDURATION %lld us\n", rv,
			rv == TEESMC_RPC_FUNC_IRQ ? "IRQ " : "", duration);
	}

	/* we needn't handle concurrency here. */
	if (duration > ctl->max_smc_time) {
		ctl->max_smc_time = duration;
	}
}

static inline void trace_tee_smc_done(tee_smc_xfer_ctl_t *ctl, s64 time_start, s64 time_end)
{
	s64 duration = time_end - time_start;

	if (duration > ctl->max_cmd_time) {
		ctl->max_cmd_time = duration;
	}
}

/* return 0 for nonpreempt rpc, 1 for others */
static int handle_nonpreempt_rpc(struct smc_param *p)
{
	uint32_t func_id = TEESMC_RETURN_GET_RPC_FUNC(p->a0);

	/* for compatibility with legacy tee-os which
		does not support clkmgr */
	if (func_id == T6SMC_RPC_CLKMGR_LEGACY_CMD) {
		p->a1 = tee_clkmgr_handle(p->a1, p->a2);
		return 0;
	}

	if (func_id != T6SMC_RPC_NONPREEMPT_CMD)
		return 1;

	switch (T6SMC_RPC_NONPREEMPT_GET_FUNC(p->a0)) {
		case T6SMC_RPC_CLKMGR_CMD:
			/* compatible with old interface */
			p->a1 = tee_clkmgr_handle(p->a1, (p->a1 & TEE_CLKMGR_TOKEN_NOT_LEGACY) ?
				p->a2 : (p->a2 | TEE_CLKMGR_OP_ENABLE));
			break;

		default:
			pr_err("Unknown non-preempt rpc cmd: 0x%llx\n", (unsigned long long) p->a0);
	}

	return 0;
}

#ifdef TKCORE_OS_PREEMPT

void xfer_preempt(tee_smc_xfer_ctl_t *ctl)
{
	int nr_cmds = atomic_read(&ctl->nr_unbound_cmds);

	/* we need not lock here, because the xfer_lock can guarantee that
	   the `nr_unbound_cmds` that we read is no larger than the actual
	   count. Therefore we won't preempt to a thread without any task */

	if (nr_cmds <= 1) {
		if (unlikely(nr_cmds == 0)) {
			pr_err("unexpected zero nr_cmds\n");
		}
	} else {
		mutex_unlock(&ctl->xfer_lock);
		/* wait for other smc to move on. probably
		   add some delay here*/
		mutex_lock(&ctl->xfer_lock);
	}
}

#endif

static void tee_smc_work(tee_smc_xfer_ctl_t *ctl, struct smc_param *p)
{
	s64 start, end;


	u64 rv = p->a0 == TEESMC32_FASTCALL_WITH_ARG ?
		TEESMC32_FASTCALL_RETURN_FROM_RPC : TEESMC32_CALL_RETURN_FROM_RPC;

	/* we need to place atomic_inc ahead of xfer_lock
	   in order that an smc-execution thread can
	   see other pending commands without releasing
	   xfer_lock */
	atomic_inc(&ctl->nr_unbound_cmds);

	mutex_lock(&ctl->xfer_lock);

	start = ktime_to_us(ktime_get());

	while (1) {

		s64 a = ktime_to_us(ktime_get()), b;

		tee_smc_call(p);

		b = ktime_to_us(ktime_get());
		trace_tee_smc(ctl, TEESMC_RETURN_GET_RPC_FUNC(p->a0), a, b);

		if (!TEESMC_RETURN_IS_RPC(p->a0))
			goto smc_return;

		if (handle_nonpreempt_rpc(p)) {
			if (TEESMC_RETURN_GET_RPC_FUNC(p->a0) == TEESMC_RPC_FUNC_IRQ) {
#ifdef TKCORE_OS_PREEMPT
				xfer_preempt(ctl);
#endif
			} else {
				/* handle other RPC commands */
				goto smc_return;
			}
		}
		p->a0 = rv;
	}

smc_return:

	atomic_dec(&ctl->nr_unbound_cmds);

	mutex_unlock(&ctl->xfer_lock);

	end = ktime_to_us(ktime_get());

	trace_tee_smc_done(ctl, start, end);
}


#ifdef TKCORE_BL

/* callers shall hold cpu->cmd_lock */
static inline void lock_cpu(tee_smc_xfer_ctl_t *ctl, int cpu)
{
	ctl->cpu_lock_mask |= (1U << cpu);
	/* prevent re-ordering */
	smp_mb();
}

/* callers shall hold cpu->cmd_lock */
static inline void unlock_cpu(tee_smc_xfer_ctl_t *ctl, int cpu)
{
	smp_mb();
	/* prevent re-ordering */
	ctl->cpu_lock_mask &= ~(1U << cpu);
}

/* can only be called in cpu notifier callback or w/ ctl->cmd_lock */
static inline bool test_cpu_locked(tee_smc_xfer_ctl_t *ctl, int cpu)
{
	return !!(ctl->cpu_lock_mask & (1U << cpu));
}

void xfer_thread_pause(tee_smc_xfer_ctl_t *ctl, int cpu)
{
	/* kthread_bind() will call wait_task_inactive(),
	   which is exactly what we need to guarantee that the task
	   is in inactive state */
	kthread_bind(ctl->xfer_threads[ctl->cpuid_to_tid[cpu]].thread, cpu);
}

void xfer_thread_resume(tee_smc_xfer_ctl_t *ctl, int cpu)
{
	/* do nothing. Since the xfer_thread remains
	   on the bound CPU, it will be woken up by
	   someone who issues an smc xfer request.
	*/
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
static int tee_cpu_callback(struct notifier_block *self,
			unsigned long action, void *hcpu)
#else
static int __cpuinit tee_cpu_callback(struct notifier_block *self,
			unsigned long action, void *hcpu)
#endif
{
	tee_smc_xfer_ctl_t *ctl = &tee_smc_xfer_ctl;
	int cpu = (int) (unsigned long) hcpu;

	if (cpu >= nr_cpus) {
		pr_err("Bad cpu: %d\n", cpu);
		return NOTIFY_BAD;
	}

	if (!cpu_is_big(cpu)) {
		return NOTIFY_OK;
	}

	switch (action & ~CPU_TASKS_FROZEN) {
		case CPU_DOWN_PREPARE:
			if (test_cpu_locked(ctl, cpu)) {
				return NOTIFY_BAD;
			} else {
				xfer_thread_pause(ctl, cpu);
			}
			break;
		case CPU_DOWN_FAILED:
			xfer_thread_resume(ctl, cpu);
			break;
		case CPU_ONLINE:
			xfer_thread_resume(ctl, cpu);
			break;
		default:
			return NOTIFY_OK;
	}

	return NOTIFY_OK;
}

/* callers shall guarantee ctl->cmd_lock is held */
static inline void __enqueue_cmd(tee_smc_xfer_ctl_t *ctl, smc_xfer_cmd_t *cmd)
{
	list_add_tail(&cmd->entry, &ctl->cmd_head);
}

/* callers shall guarantee ctl->cmd_lock is held */
static inline smc_xfer_cmd_t *__dequeue_cmd(tee_smc_xfer_ctl_t *ctl)
{
	smc_xfer_cmd_t *cmd;

	if (list_empty(&ctl->cmd_head)) {
		return NULL;
	}

	cmd = list_first_entry(&ctl->cmd_head, smc_xfer_cmd_t, entry);
	list_del(&cmd->entry);

	return cmd;
}

static smc_xfer_cmd_t *wait_dequeue_cmd(tee_smc_xfer_ctl_t *ctl, int cpu)
{
	int idx;
	smc_xfer_cmd_t *cmd = NULL;

	spin_lock(&ctl->cmd_lock);
	idx = ctl->cpuid_to_tid[cpu];

wait_start:
	{
		DEFINE_WAIT(wait);
		prepare_to_wait(&ctl->xfer_threads[idx].thread_wq, &wait, TASK_UNINTERRUPTIBLE);

		if ((cmd = __dequeue_cmd(ctl)))  {
			goto wait_end;
		} else {
			unlock_cpu(ctl, cpu);
			spin_unlock(&ctl->cmd_lock);

			schedule();

			spin_lock(&ctl->cmd_lock);
			finish_wait(&ctl->xfer_threads[idx].thread_wq, &wait);
			goto wait_start;
		}

wait_end:
		finish_wait(&ctl->xfer_threads[idx].thread_wq, &wait);
		spin_unlock(&ctl->cmd_lock);
	}

	return cmd;
}

#ifdef TKCORE_OS_PREEMPT

static bool xfer_preempt_l(tee_smc_xfer_ctl_t *ctl, smc_xfer_cmd_t *cmd)
{
	spin_lock(&ctl->cmd_lock);

	if (ctl->cpu_lock_mask) {
		int r;

		atomic_dec(&ctl->nr_unbound_cmds);

		__enqueue_cmd(ctl, cmd);
		mutex_unlock(&ctl->xfer_lock);

		spin_unlock(&ctl->cmd_lock);

		do {
			/*TODO handle timing */
			r = down_interruptible(&cmd->done_sem);
		} while (r);

		/* We need not lock(xfer_lock) again because
		   the smc command is already completed */
		return true;
	} else {
		spin_unlock(&ctl->cmd_lock);
		xfer_preempt(ctl);

		return false;
	}

}

/* xfer_preempt_l unlocks xfer_lock */
static smc_xfer_cmd_t *xfer_preempt_b(tee_smc_xfer_ctl_t *ctl, smc_xfer_cmd_t *cmd)
{
	smc_xfer_cmd_t *new_cmd;

	/* first check cmd_head w/o acquiring
	   lock */
	if (list_empty(&ctl->cmd_head)) {
		return cmd;
	}

	spin_lock(&ctl->cmd_lock);

	/* exchange cmd with cmd in queue */
	if ((new_cmd = __dequeue_cmd(ctl)) == NULL) {
		spin_unlock(&ctl->cmd_lock);
		return cmd;
	}

	__enqueue_cmd(ctl, cmd);

	spin_unlock(&ctl->cmd_lock);

	return new_cmd;
}

#endif

static void tee_smc_work_bl(tee_smc_xfer_ctl_t *ctl, bool big, smc_xfer_cmd_t *cmd)
{
	s64 start, end;
	struct smc_param *p = cmd->param;

	u64 rv = p->a0 == TEESMC32_FASTCALL_WITH_ARG ?
		TEESMC32_FASTCALL_RETURN_FROM_RPC : TEESMC32_CALL_RETURN_FROM_RPC;

	if (!big)
		atomic_inc(&ctl->nr_unbound_cmds);

	mutex_lock(&ctl->xfer_lock);

	start = ktime_to_us(ktime_get());

	while (1) {

		s64 a = ktime_to_us(ktime_get()), b;

		tee_smc_call(p);

		b = ktime_to_us(ktime_get());
		trace_tee_smc(ctl, TEESMC_RETURN_GET_RPC_FUNC(p->a0), a, b);

		if (!TEESMC_RETURN_IS_RPC(p->a0))
			goto smc_return;

		if (handle_nonpreempt_rpc(p)) {
			if (TEESMC_RETURN_GET_RPC_FUNC(p->a0) == TEESMC_RPC_FUNC_IRQ) {
#ifdef TKCORE_OS_PREEMPT
				if (big == false) {
					if (xfer_preempt_l(ctl, cmd)) {
						goto smc_l_ret;
					}
				} else {
					p->a0 = rv;
					cmd = xfer_preempt_b(ctl, cmd);
					p = cmd->param;
					continue;
				}
#endif
			} else {
				/* handle other RPC commands */
				goto smc_return;
			}
		}

		p->a0 = rv;
	}

smc_return:

	if (!big)
		atomic_dec(&ctl->nr_unbound_cmds);

	mutex_unlock(&ctl->xfer_lock);

#ifdef TKCORE_OS_PREEMPT
smc_l_ret:
#endif
	end = ktime_to_us(ktime_get());

	trace_tee_smc_done(ctl, start, end);
}

static int tee_smc_daemon(void *data)
{
	int cpu = (int) (unsigned long) data;
	tee_smc_xfer_ctl_t *ctl = &tee_smc_xfer_ctl;

	if (cpu < 0 || cpu >= nr_cpus) {
		pr_err("bad cpu: %d\n", cpu);
		return 0;
	}

	while (!kthread_should_stop()) {
		smc_xfer_cmd_t *cmd;

		cmd = wait_dequeue_cmd(ctl, cpu);

		tee_smc_work_bl(ctl, true, cmd);

		up(&cmd->done_sem);
	}

	return 0;
}


static void __smc_xfer(tee_smc_xfer_ctl_t *ctl, struct smc_param *p)
{
	int picked_cpu = -1;
	int cpu;
	bool need_wake_up = false;

	smc_xfer_cmd_t cmd;

	s64 smc_xfer_start;

	if (!ctl->platform_is_big_little) {
		tee_smc_work(ctl, p);
		return;
	}
	/* initialize command */
	cmd.param = p;
	sema_init(&cmd.done_sem, 0);
	INIT_LIST_HEAD(&cmd.entry);

	smc_xfer_start = ktime_to_us(ktime_get());

pick_smc_cpu:
	get_online_cpus();

	spin_lock(&ctl->cmd_lock);

	if (ctl->cpu_lock_mask == 0) {
		for_each_online_cpu(cpu) {
			if (cpu_is_big(cpu)) {
				picked_cpu = cpu;

				lock_cpu(ctl, picked_cpu);
				need_wake_up = true;

				break;
			}
		}
	} else {
		int i;
		for (i = 0; i < nr_cpus; i++) {
			if (ctl->cpu_lock_mask & (1U << i)) {
				picked_cpu = i;
				break;
			}
		}
	}

	put_online_cpus();

	if (picked_cpu != -1) {
		int idx;

		__enqueue_cmd(ctl, &cmd);

		spin_unlock(&ctl->cmd_lock);

		/* wake up the tee_smc_daemon on picked_cpu */
		idx = ctl->cpuid_to_tid[picked_cpu];

		if (need_wake_up)
			wake_up(&ctl->xfer_threads[idx].thread_wq);

		/*TODO handle timing */
		down(&cmd.done_sem);

	} else {

		s64 smc_xfer_curr;

		spin_unlock(&ctl->cmd_lock);

		smc_xfer_curr = ktime_to_us(ktime_get());

		if (smc_xfer_curr - smc_xfer_start < smc_xfer_timeout_us) {
			int r = cpu_up(ctl->candidate_big_cpu);
			if (r == 0) {
				goto pick_smc_cpu;
			} else {
				pr_err("cpu_on(%d) failed with ret: %d\n",
					ctl->candidate_big_cpu, r);
				goto pick_smc_cpu;
			}
		} else {
			tee_smc_work_bl(ctl, false, &cmd);
		}
	}
}

static bool cpu_is_big(int cpu)
{
	/* consider cpu on cluster 1 to be big*/
	return arch_get_cluster_id(cpu) == 1;
}

static int platform_bl_init(tee_smc_xfer_ctl_t *ctl)
{
	int i = 0, r;

	int nr_big_cpus = 0;
	int cpu, candidate_big_cpu = -1, max_big_cpu = -1;

	if (!ctl->platform_is_big_little)
		return 0;

	/* we choose the first big cpu as the
		candidate big cpu */
	for_each_possible_cpu(cpu) {
		if (cpu_is_big(cpu)) {
			if (candidate_big_cpu < 0) {
				candidate_big_cpu = cpu;
			}
			max_big_cpu = max_big_cpu < cpu ? cpu : max_big_cpu;
			++nr_big_cpus;
		}
	}

	if (nr_big_cpus == 0) {
		/* the big cpu does not exist. thus
			we fallback to non-bl sched case */
		ctl->platform_is_big_little = false;
		return 0;
	}

	ctl->candidate_big_cpu = candidate_big_cpu;

	ctl->cpu_lock_mask = 0U;

	INIT_LIST_HEAD(&ctl->cmd_head);
	spin_lock_init(&ctl->cmd_lock);

	ctl->xfer_threads_count = nr_big_cpus;

	if ((ctl->xfer_threads = kzalloc(sizeof(ctl->xfer_threads[0]) * nr_big_cpus, GFP_KERNEL)) == NULL) {
		pr_err("bad kmalloc %zu bytes for xfer_threads\n",
			sizeof(ctl->xfer_threads[0]) * nr_big_cpus);
		r = -ENOMEM;
		goto err;
	}

	memset(ctl->cpuid_to_tid, 0, sizeof(ctl->cpuid_to_tid));

	for_each_possible_cpu(cpu) {
		if (cpu_is_big(cpu)) {
			/* since it's the initialization phase we are free to
			   call lock_cpu() without holding lock */
			lock_cpu(ctl, cpu);
			ctl->cpuid_to_tid[cpu] = i;
			init_waitqueue_head(&ctl->xfer_threads[i].thread_wq);
			if (IS_ERR(ctl->xfer_threads[i].thread = kthread_create(
					tee_smc_daemon, (void *) (unsigned long) cpu,
					"tkcore_smc_xfer.%u", (unsigned int) cpu))) {
				pr_err("failed to start tkcore_smc_xfer%d: %p\n",
					cpu, ctl->xfer_threads[i].thread);
				r = -1;
				goto err;
			}

			kthread_bind(ctl->xfer_threads[i].thread, cpu);
			++i;
		}
	}

	for (i = 0; i < ctl->xfer_threads_count; i++) {
		wake_up_process(ctl->xfer_threads[i].thread);
	}

	/* prevent the re-ordering between wakeup_process
		and tee_smc_tsk_running = true.*/
	smp_mb();

	ctl->xfer_thread_running = true;

	ctl->tee_cpu_notifier.notifier_call = tee_cpu_callback;
	register_cpu_notifier(&ctl->tee_cpu_notifier);

	return 0;
err:

	if (ctl->xfer_threads) {
		for (i = 0; i < ctl->xfer_threads_count; i++) {
			if (!IS_ERR(ctl->xfer_threads[i].thread))
				kthread_stop(ctl->xfer_threads[i].thread);
			else
				break;
		}
		kfree(ctl->xfer_threads);
		ctl->xfer_threads = NULL;
	}

	return r;
}

static void platform_bl_deinit(tee_smc_xfer_ctl_t *ctl)
{
	int i;

	if (!ctl->platform_is_big_little)
		return;

	ctl->xfer_thread_running = false;

	smp_mb();

	for (i = 0; i < ctl->xfer_threads_count; i++) {
		kthread_stop(ctl->xfer_threads[i].thread);
	}

	kfree(ctl->xfer_threads);
	ctl->xfer_threads = NULL;

	unregister_cpu_notifier(&ctl->tee_cpu_notifier);
}

#else

static inline void __smc_xfer(tee_smc_xfer_ctl_t *ctl, struct smc_param *p)
{
	tee_smc_work(ctl, p);
}

static int platform_bl_init(tee_smc_xfer_ctl_t *ctl) { return 0; }

static void platform_bl_deinit(tee_smc_xfer_ctl_t *ctl) { }

#endif

static void xfer_enqueue_waiters(tee_smc_xfer_ctl_t *ctl)
{
	int r;
	atomic_inc(&ctl->nr_waiting_cmds);

	do {
		r = down_interruptible(&ctl->xfer_wait_queue);
		/*TODO handle too long time of waiting */
	} while (r);
}

static void xfer_dequeue_waiters(tee_smc_xfer_ctl_t *ctl)
{
	if (atomic_dec_if_positive(&ctl->nr_waiting_cmds) >= 0) {
		up(&ctl->xfer_wait_queue);
	}
}

void __call_tee(struct smc_param *p)
{
	/* NOTE!!! we remove the e_lock_teez(ptee) here !!!! */
	for (;;) {
		__smc_xfer(&tee_smc_xfer_ctl, p);
		if (p->a0 == TEESMC_RETURN_ETHREAD_LIMIT) {
			xfer_enqueue_waiters(&tee_smc_xfer_ctl);
		} else {
			if (!TEESMC_RETURN_IS_RPC(p->a0))
				xfer_dequeue_waiters(&tee_smc_xfer_ctl);
			break;
		}
	}
}

inline void smc_xfer(struct smc_param *p)
{
	__smc_xfer(&tee_smc_xfer_ctl, p);
}

int tee_init_smc_xfer(void)
{
	int r;
	tee_smc_xfer_ctl_t *ctl = &tee_smc_xfer_ctl;

#ifdef TKCORE_BL
	nr_cpus = num_possible_cpus();

	if (nr_cpus > NR_CPUS) {
		pr_err("nr_cpus %d exceeds NR_CPUS %d\n", nr_cpus, NR_CPUS);
		return -1;
	}

	ctl->platform_is_big_little = !arch_is_smp();
	pr_info("platform is bL: %d\n", ctl->platform_is_big_little);
#endif

	atomic_set(&ctl->nr_unbound_cmds, 0);
	mutex_init(&ctl->xfer_lock);

	atomic_set(&ctl->nr_waiting_cmds, 0);
	sema_init(&ctl->xfer_wait_queue, 0);

	ctl->max_smc_time = 0LL;
	ctl->max_cmd_time = 0LL;

	if ((r = platform_bl_init(ctl)) < 0) {
		goto err;
	}

	return 0;

err:
	return r;
}

void tee_exit_smc_xfer(void)
{
	tee_smc_xfer_ctl_t *ctl = &tee_smc_xfer_ctl;

	platform_bl_deinit(ctl);

}
