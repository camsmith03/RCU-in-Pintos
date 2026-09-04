#ifndef SELFTEST_SIM_RUNNER_H
#define SELFTEST_SIM_RUNNER_H

#include "selftest/test_types.h"

void start_simulation(enum test_type, enum concurrency_type,
                      enum access_pattern);

#endif
