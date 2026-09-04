#ifndef SELFTEST_RCU_COW_TEST_H
#define SELFTEST_RCU_COW_TEST_H

void rcu_cow_reader(void *args);
void rcu_cow_sync_writer(void *args);
void rcu_cow_async_writer(void *args);

#endif
