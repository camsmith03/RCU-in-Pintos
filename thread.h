#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#define RCU_GP_TICK_INTR 10 


void thread_block_gp_thread (void);
void thread_spawn_gp_thread (thread_func gp_func);
void thread_kill_gp_thread (void);


#endif 
