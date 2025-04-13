#if defined(__ARM_ARCH_ISA_A64)
// ARM64 based implementation

static inline __u64 rdtsc(void)
{
	__u64 cntvct;
	asm volatile("mrs %0, cntvct_el0; " : "=r"(cntvct)::"memory");
	return cntvct;
}

static inline __u64 rdtsc_precise(void)
{
	__u64 cntvct;
	asm volatile("isb; mrs %0, cntvct_el0; isb; " : "=r"(cntvct)::"memory");
	return cntvct;
}

#elif defined(__x86_64__)
// AMD64 based implementation

static inline __u64 rdtsc(void)
{
	union {
		__u64 tsc_64;
		struct {
			__u32 lo_32;
			__u32 hi_32;
		};
	} tsc;

	asm volatile("rdtsc" : "=a"(tsc.lo_32), "=d"(tsc.hi_32));

	return tsc.tsc_64;
}

static inline __u64 rdtsc_precise(void)
{
	asm volatile("mfence");
	return rdtsc();
}

#endif

#define NS_PER_SEC 1E9

static uint64_t get_tsc_freq(void)
{
	struct timespec sleeptime = { .tv_nsec = NS_PER_SEC / 10 }; /* 1/10 second */
	struct timespec t_start, t_end;
	uint64_t tsc_hz, ns, end, start;

	if (clock_gettime(1, &t_start) != 0)
		return 0;

	start = rdtsc();
	nanosleep(&sleeptime, NULL);

	if (clock_gettime(1, &t_end) != 0)
		return 0;

	end = rdtsc();
	ns = ((t_end.tv_sec - t_start.tv_sec) * NS_PER_SEC);
	ns += (t_end.tv_nsec - t_start.tv_nsec);

	double secs = (double)ns / NS_PER_SEC;
	tsc_hz = (uint64_t)((end - start) / secs);

	return tsc_hz;
}

static uint64_t __hz;

static uint64_t get_timer_hz(void)
{
	if (__hz == 0)
		__hz = get_tsc_freq();

	return __hz;
}
