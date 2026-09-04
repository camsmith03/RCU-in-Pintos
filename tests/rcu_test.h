#ifndef SELFTEST_RCU_TEST_H
#define SELFTEST_RCU_TEST_H

void rcu_reader(void *args);
void rcu_sync_writer(void *args);
void rcu_async_writer(void *args);

#endif
