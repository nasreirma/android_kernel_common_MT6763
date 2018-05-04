#ifndef TKCORE_PM_H
#define TKCORE_PM_H

typedef int (* awake_fn) (void *);
int tkcore_stay_awake(awake_fn fn, void *data);

int tkcore_tee_pm_init(void);

void tkcore_tee_pm_exit(void);

#endif
