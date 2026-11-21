#ifndef DISPATCHER_H
#define DISPATCHER_H

#include <functional>

using job_t = std::function<void(void)>;

#ifdef __cplusplus
extern "C"
{
#endif
    extern void dispatcher_init();
    extern void dispatch_job(job_t job);

#ifdef __cplusplus
}
#endif

#endif