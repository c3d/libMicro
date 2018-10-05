/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms
 * of the Common Development and Distribution License
 * (the "License").  You may not use this file except
 * in compliance with the License.
 *
 * You can obtain a copy of the license at
 * src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL
 * HEADER in each file and include the License file at
 * usr/src/OPENSOLARIS.LICENSE.  If applicable,
 * add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your
 * own identifying information: Portions Copyright [yyyy]
 * [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Modifications by Red Hat, Inc.
 */

/*
 * benchmarking framework
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/resource.h>
#include <math.h>
#include <limits.h>
#include <assert.h>
#include <time.h>
#include <sys/signalfd.h>

#ifdef	__sun
#include <sys/elf.h>
#endif

#include "libmicro.h"

#include "recorder/recorder.c"
#include "recorder/recorder_ring.c"

#define HISTOSIZE	32
#define DEF_DATASIZE	100000
#define MIN_DATASIZE	20000

#define DEF_SAMPLES	100
#define DEF_TIME	10 /* seconds */
#define MAX_TIME	600 /* seconds, or 10 minutes */

/*
 * user visible globals
 */

int				lm_argc = 0;
char		  **lm_argv = NULL;

int				lm_opt1;
int				lm_optA;
int				lm_optB = 0;
int				lm_optC = 0;
int				lm_optD;
int				lm_optE;
int				lm_optG = 0;
int				lm_optH;
int				lm_optI = 0;
int				lm_optL = 0;
int				lm_optM = 0;
char		   *lm_optN;
unsigned int	lm_optO = 0;
int				lm_optP;
unsigned int	lm_optR = 0;
int				lm_optS;
int				lm_optT;
int				lm_optW;
int				lm_optX;

int				lm_def1 = 0;
int				lm_defC = DEF_SAMPLES;
int				lm_defD = DEF_TIME*1000; /* DEF_TIME ms */
int				lm_defH = 0;
int				lm_defI = 500*1000; /* 500us per op */
char		   *lm_defN = NULL;
int				lm_defP = 1;
int				lm_defS = 0;
int				lm_defT = 1;
int				lm_defX = MAX_TIME*1000; /* MAX_TIME ms */

char		   *lm_procpath;
char			lm_procname[STRSIZE];
char			lm_usage[STRSIZE];
char			lm_optstr[STRSIZE];
char			lm_header[STRSIZE];
size_t			lm_tsdsize = 0;
pthread_t		lm_default_thread = (pthread_t)NULL;
int				lm_dynamic_optB = 1;

/*
 *	Globals we do not export to the user
 */

static barrier_t   *lm_barrier;
static pid_t	   *pids = NULL;
static pthread_t   *tids = NULL;
static int			pindex = -1;
static void		   *tsdseg = NULL;
static size_t		tsdsize = 0;

#ifdef USE_RDTSC
static long long	lm_hz = 0;
#endif

/*
 * Forward references
 */

static int			worker_process(void);
static void			usage(void);
static void			print_stats(barrier_t *);
static void			print_histo(barrier_t *);
static int			remove_outliers(long long *, int, stats_t *);
static unsigned int	nsecs_overhead;
static unsigned int	nsecs_resolution;
static void			crunch_stats(long long *, int, stats_t *);
static void			compute_stats(barrier_t *);
static void			fit_line(long long *, long long *, int, double *, double *);
static void		   *gettsd(int, int);

/*
 * main routine; renamed in this file to allow linking with other
 * files
 */

int
actual_main(int argc, char *argv[])
{
	int				i, ret;
	int				opt;
	extern char		*optarg;
	char			*tmp;
	char			optstr[256];
	barrier_t		*b;
	long long		startnsecs;

        recorder_trace_set(getenv("RECORDER_TRACES"));
#ifdef USE_RDTSC
	if (getenv("LIBMICRO_HZ") == NULL) {
		(void) printf("LIBMICRO_HZ needed but not set\n");
		return 1;
	}
	lm_hz = strtoll(getenv("LIBMICRO_HZ"), NULL, 10);
#endif

	startnsecs = getnsecs();

	lm_argc = argc;
	lm_argv = argv;

	/* before we do anything, give benchmark a chance to modify the defaults */
	ret = benchmark_init();
	if (ret != 0)
		return ret;

	/* check that the case defines lm_tsdsize before proceeding */
	if (lm_tsdsize == (size_t)-1) {
		(void) fprintf(stderr, "error in benchmark_init: "
				"lm_tsdsize not set\n");
		return 1;
	}

	/*
	 * Set defaults
	 */

	lm_opt1	= lm_def1;
	lm_optD	= lm_defD;
	lm_optH	= lm_defH;
	lm_optI = lm_defI;
	lm_optN	= lm_defN;
	lm_optP	= lm_defP;
	lm_optX = lm_defX;

	lm_optS	= lm_defS;
	lm_optT	= lm_defT;

	/*
	 * squirrel away the path to the current
	 * binary in a way that works on both
	 * Linux and Solaris
	 */

	if (*argv[0] == '/') {
		lm_procpath = strdup(argv[0]);
		*strrchr(lm_procpath, '/') = 0;
	} else {
		char path[1024];
		(void) getcwd(path, 1024);
		(void) strcat(path, "/");
		(void) strcat(path, argv[0]);
		*strrchr(path, '/') = 0;
		lm_procpath = strdup(path);
	}

	/*
	 * name of binary
	 */

	if ((tmp = strrchr(argv[0], '/')) == NULL)
		(void) strcpy(lm_procname, argv[0]);
	else
		(void) strcpy(lm_procname, tmp + 1);

	if (lm_optN == NULL) {
		lm_optN = lm_procname;
	}

	/*
	 * Parse command line arguments
	 */

	(void) snprintf(optstr, sizeof(optstr), "1AB:C:D:EG:HI:LMN:O:P:R:ST:VWX:?%s", lm_optstr);
	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
		case '1':
			lm_opt1 = 1;
			break;
		case 'A':
			lm_optA = 1;
			break;
		case 'B':
			lm_optB = sizetoint(optarg);
			lm_dynamic_optB = 0;
			break;
		case 'C':
			lm_optC = sizetoint(optarg);
			if (lm_optC <= 0) {
				if (lm_optD <= 0) {
					(void) printf("warning: '-C %d' <= 0 and '-D %d' <= 0, defaulting '-D' to %d\n", lm_optC, lm_optD, lm_defD);
					lm_optD = lm_defD;
				}
			}
			break;
		case 'D':
			lm_optD = sizetoint(optarg);
			if (lm_optD <= 0) {
				if (lm_optC <= 0) {
					(void) printf("warning: '-D %d' <= 0 and '-C %d' <= 0, defaulting '-C' to %d\n", lm_optD, lm_optC, lm_defC);
					lm_optC = lm_defC;
				}
			}
			break;
		case 'E':
			lm_optE = 1;
			break;
		case 'G':
			lm_optG = atoi(optarg);
			break;
		case 'H':
			lm_optH = 1;
			break;
		case 'I':
			lm_optI = sizetoint(optarg);
			lm_dynamic_optB = 0;
			break;
		case 'L':
			lm_optL = 1;
			break;
		case 'M':
			lm_optM = 1;
			break;
		case 'N':
			lm_optN = optarg;
			break;
		case 'O':
			lm_optO = (unsigned int)atoi(optarg);
			break;
		case 'P':
			lm_optP = sizetoint(optarg);
			break;
		case 'R':
			lm_optR = (unsigned int)atoi(optarg);
			break;
		case 'S':
			lm_optS = 1;
			break;
		case 'T':
			lm_optT = sizetoint(optarg);
			break;
		case 'V':
			(void) printf("%s\n", LIBMICRO_VERSION);
			return 0;
		case 'W':
			lm_optW = 1;
			lm_optS = 1;
			break;
		case 'X':
			lm_optX = sizetoint(optarg);
			if (lm_optX < 0) {
				(void) printf("warning: '-X %d' < 0, defaulting to %dms\n", lm_optX, lm_defX);
				lm_optX = lm_defX;
			}
			else if (lm_optX > 0 && lm_optX < lm_optD) {
				(void) printf("warning: '-X %d' < '-D %d', ignoring -X value\n", lm_optX, lm_optD);
				lm_optX = 0;
			}
			break;
		case '?':
			usage();
			return 0;
		default:
			if (benchmark_optswitch(opt, optarg) == -1) {
				usage();
				return 1;
			}
		}
	}

	/*
	 * We have to have at least one method of ending the test set, allowing
	 * for both to allow for -C specifying the minimum number of Runs and -D
	 * the minimum amount of time to run.
	 */
	assert((lm_optC > 0 && lm_optD >= 0) || (lm_optC >= 0 && lm_optD > 0));
	assert(lm_optX == 0 || lm_optX > lm_optD);

	if (lm_optO > 0) {
		nsecs_overhead = lm_optO;
	}
	else {
		nsecs_overhead = get_nsecs_overhead();
	}

	if (lm_optR > 0) {
		nsecs_resolution = lm_optR;
	}
	else {
		nsecs_resolution = get_nsecs_resolution();
	}

	/* deal with implicit and overriding options */
	if (lm_opt1 && lm_optP > 1) {
		lm_optP = 1;
		(void) printf("warning: -1 overrides -P\n");
	}

	if (lm_optE) {
		(void) fflush(stdout);
		(void) fprintf(stderr, "Running:%30s ", lm_optN);
		(void) fflush(stderr);
	}

	if (lm_optB == 0) {
		/*
		 * Neither benchmark or user has specified the number of cnts/sample,
		 * so use a computed initial value.
		 *
		 * In a DEF_TIME second period (see lm_optD above), we want to have
		 * DEF_SAMPLES samples executed in that period. So each batch size
		 * should run for about DEF_TIME/100 seconds.
		 */
		lm_optB = (int)round((double)(1000 * 1000) / lm_optI);

		if (lm_optB == 0) {
			if (lm_optG >= 1) fprintf(stderr, "DEBUG1 (%s): (1000 * 1000) / lm_optI (%d)) == 0, defaulting lm_optB to one (1)\n", lm_optN, lm_optI);
			lm_optB = 1;
		}
		else if (lm_optG >= 2) {
			fprintf(stderr, "DEBUG2 (%s): defaulting lm_optB to %d\n", lm_optN, lm_optB);
		}
	}

	/*
	 * now that the options are set
	 */

	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main() calling benchmark_initrun()\n");
	ret = benchmark_initrun();
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main() benchmark_initrun() returned %d\n", ret);
	if (ret == -1) {
		return 1;
	}

	/* allocate dynamic data */
	pids = (pid_t *)malloc(lm_optP * sizeof (pid_t));
	if (NULL == pids) {
		perror("malloc(pids)");
		return 1;
	}
	tids = (pthread_t *)malloc(lm_optT * sizeof (pthread_t));
	if (NULL == tids) {
		perror("malloc(tids)");
		return 1;
	}

	/* round up tsdsize to nearest 128 to eliminate false sharing */
	tsdsize = ((lm_tsdsize + 127) / 128) * 128;

	/* allocate sufficient TSD for each thread in each process */
	tsdseg = (void *)mmap(NULL, lm_optT * lm_optP * tsdsize + 8192,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0L);
	if (tsdseg == NULL) {
		perror("mmap(tsd)");
		return 1;
	}

	/* initialise worker synchronisation */
	b = barrier_create(lm_optT * lm_optP, DEF_DATASIZE);
	if (b == NULL) {
		perror("barrier_create()");
		return 1;
	}
	lm_barrier = b;

	/* need this here so that parent and children can call exit() */
	(void) fflush(stdout);
	(void) fflush(stderr);

	/* when we started and when to stop */

	b->ba_starttime = getnsecs();
	b->ba_minruntime = b->ba_starttime + (lm_optD * 1000000LL);

	if (lm_optX > 0)
		b->ba_deadline = b->ba_starttime + (lm_optX * 1000000LL);
	else if (lm_optC <= 0)
		b->ba_deadline = b->ba_minruntime;
	else
		b->ba_deadline = 0;

	int exit_val;

	/* do the work */
	if (lm_opt1) {
		/* single process, non-fork mode */
		pindex = 0;
		exit_val = worker_process();
	} else {
		sigset_t mask;

		sigemptyset(&mask);
		sigaddset(&mask, SIGALRM);
		sigaddset(&mask, SIGCHLD);
		sigaddset(&mask, SIGINT);
		sigaddset(&mask, SIGHUP);
		sigaddset(&mask, SIGTERM);
		sigaddset(&mask, SIGQUIT);
		int ret = sigprocmask(SIG_BLOCK, &mask, NULL);
		if (ret < 0) {
			perror("sigprocmask");
			return 1;
		}

		int sigfd = signalfd(-1, &mask, SFD_CLOEXEC);
		if (sigfd == -1) {
			perror("signalfd");
			return 1;
		}

		/* create worker processes */
		for (i = 0; i < lm_optP; i++) {
			pids[i] = fork();

			switch (pids[i]) {
			case 0:
				pindex = i;
				exit(worker_process());
				break;
			case -1:
				perror("fork");
				return 1;
				break;
			default:
				continue;
			}
		}

		timer_t host_timer = 0;
		struct itimerspec timeout = { 0 };
		if (b->ba_deadline > 0) {
			ret = timer_create(CLOCK_MONOTONIC, NULL, &host_timer);
			if (ret < 0) {
				perror("timer_create");
				return 1;
			}

			/* Kill the test if it goes one minute over the deadline */
			timeout.it_value.tv_sec = (b->ba_deadline / 1000000000LL) + 60;
			ret = timer_settime(host_timer, TIMER_ABSTIME, &timeout, NULL);
			if (ret < 0) {
				perror("timer_settime");
				return 1;
			}
		}

		// Assume child processes will have succeeded.
		exit_val = 0;

		int done = 0;
		do {
			struct signalfd_siginfo fdsi;
			memset(&fdsi, 0, sizeof(struct signalfd_siginfo));
			ssize_t s = read(sigfd, &fdsi, sizeof(struct signalfd_siginfo));
			if (s != sizeof(struct signalfd_siginfo)) {
				perror("read(signalfd)");
				return 1;
			}

			if (fdsi.ssi_signo == SIGALRM
					|| fdsi.ssi_signo == SIGINT) {
				if (fdsi.ssi_signo == SIGALRM) {
					b->ba_killed = KILLED_LONG;
				}
				else {
					b->ba_killed = KILLED_INT;
					if (0 == exit_val) exit_val = 1;
				}

				/* kill the worker processes */
				for (i = 0; i < lm_optP; i++) {
					if (pids[i] > 0) {
						int ret = kill(pids[i], SIGKILL);
						if (ret < 0) {
							if (errno != ESRCH) {
								perror("kill");
								return 1;
							}
						}
					}
				}

				done = 1;
			}
			else if (fdsi.ssi_signo == SIGCHLD) {
				/* wait for worker processes */
				int status;
				int waiting = 0;
				for (i = 0; i < lm_optP; i++) {
					if (pids[i] > 0) {
						int ret = waitpid(pids[i], &status, WNOHANG);
						if (ret < 0) {
							perror("waitpid");
							return 1;
						}
						if (WIFEXITED(status) || WIFSIGNALED(status)) {
							pids[i] = 0;
							if (WIFEXITED(status)) {
								int e_val = WEXITSTATUS(status);
								if ((0 != e_val) && (0 == exit_val)) exit_val = e_val;
							}
							else {
								if (0 == exit_val) exit_val = 1;
							}
						}
						else {
							waiting++;
						}
					}
				}
				if (!waiting) {
					done = 1;
				}
			}
		} while (!done);

		if (host_timer > 0) {
			ret = timer_delete(host_timer);
			if (ret < 0) {
				perror("timer_delete");
			}
		}
	}

	b->ba_endtime = getnsecs();

	/* compute results */

	compute_stats(b);

	if (lm_optE) {
		(void) fflush(stdout);
		(void) fprintf(stderr, "for %12.5f seconds\n",
				(double)(getnsecs() - startnsecs) / 1.e9);
		(void) fflush(stderr);
	}

#define RES_WIDTH	15
#define RES_PREC	 5

	/* print result header (unless suppressed) */
	if (!lm_optH) {
		(void) printf("%*s %3s %3s %*s %12s %8s %8s %s\n",
				strlen(lm_optN), "", "prc", "thr",
				RES_WIDTH, "nsecs/call",
				"samples", "errors", "cnt/samp", lm_header);
	}

	/* print result */

	double usedB = (double)b->ba_count / b->ba_batches;

	if (lm_optM) {
		(void) printf("%-*s %3d %3d %*.*f %12d %8lld %8d %s\n",
				strlen(lm_optN), lm_optN, lm_optP, lm_optT,
				RES_WIDTH, RES_PREC, b->ba_corrected.st_mean,
				b->ba_batches_final, b->ba_errors, (int)usedB,
				benchmark_result());
	}
	else {
		(void) printf("%-*s %3d %3d %*lld %12d %8lld %8d %s\n",
				strlen(lm_optN), lm_optN, lm_optP, lm_optT,
				RES_WIDTH, b->ba_corrected.st_median,
				b->ba_batches_final, b->ba_errors, (int)usedB,
				benchmark_result());
	}

	/* print arguments benchmark was invoked with ? */
	if (lm_optL) {
		int l;
		(void) printf("# %s ", argv[0]);
		for (l = 1; l < argc; l++) {
			(void) printf("%s ", argv[l]);
		}
		(void) printf("\n");
	}

	if (lm_optS) {
		print_stats(b);
	}

	/* just incase something goes awry */
	(void) fflush(stdout);
	(void) fflush(stderr);

	/* cleanup by stages */
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): calling benchmark_finirun()\n");
	ret = benchmark_finirun();
	if (0 == exit_val) exit_val = ret;
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): benchmark_finirun() returned %d\n", ret);

	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): calling barrier_destroy()\n");
	ret = barrier_destroy(b);
	if (0 == exit_val) exit_val = ret;
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): barrier_destroy() returned %d\n", ret);

	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): calling benchmark_fini()\n");
	ret = benchmark_fini();
	if (0 == exit_val) exit_val = ret;
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): benchmark_fini() returned %d\n", ret);

	return exit_val;
}


RECORDER_DEFINE(benchmark, 8, "Raw results of benchmark as it's running");

static void *
worker_thread(void *arg)
{
	result_t	r;
	long long	last_sleep = 0;
	long long	t;
	int			ret, retb;
	void		*ret_val = NULL;

	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): calling benchmark_initworker()\n", getpid(), pthread_self());
	r.re_errors = ret = benchmark_initworker(arg);
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): benchmark_initworker() returned %d\n", getpid(), pthread_self(), ret);

	do {
		r.re_count = 0;
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): calling benchmark_initbatch()\n", getpid(), pthread_self());
		r.re_errors += ret = benchmark_initbatch(arg);
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): benchmark_initbatch() returned %d\n", getpid(), pthread_self(), ret);

		/* sync to clock */

		if (lm_optA && ((t = getnsecs()) - last_sleep) > 75000000LL) {
			(void) poll(0, 0, 10);
			last_sleep = t;
		}

		/* wait for it ... */
		retb = barrier_queue(lm_barrier, NULL);
		if (retb < 0) {
			return (void *)1;
		}

		assert (0 == retb);

		/* time the test */
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): calling benchmark()\n", getpid(), pthread_self());
		r.re_t0 = getnsecs();
		ret = benchmark(arg, &r);
		r.re_t1 = getnsecs();

                record(benchmark, "Ran %lld loops in %lld ns (%5.3f ns average)",
                       r.re_count, r.re_t1 - r.re_t0,
                       (1.0 * (r.re_t1 - r.re_t0) / r.re_count));


        if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): benchmark() returned %d\n", getpid(), pthread_self(), ret);

		/* record results and sync */
		retb = barrier_queue(lm_barrier, &r);
		if (retb < 0) {
			return (void *)1;
		}

		/* Errors from finishing this batch feed into the next batch */
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): calling benchmark_finibatch()\n", getpid(), pthread_self());
		r.re_errors = ret = benchmark_finibatch(arg);
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): benchmark_finibatch() returned %d\n", getpid(), pthread_self(), ret);

		if ((0 == retb) && lm_dynamic_optB) {
			// Ensure all threads have run their finibatch code.
			retb = barrier_queue(lm_barrier, NULL);
			if (retb < 0) {
				return (void *)1;
			}
			if (pthread_self() == lm_default_thread) {
				/*
				 * Only the default thread makes the change to lm_optB. Note
				 * that this is the default thread in each process, so while
				 * multiple threads, one from each process, are doing this,
				 * they will all arrive at the same answer (since they all use
				 * the same data) but only change the lm_optB in their own
				 * process.
				 */
				long long		sum = 0;
				unsigned int	i;
				long long		count = (lm_barrier->ba_batches >= lm_barrier->ba_datasize)
										? lm_barrier->ba_datasize
										: lm_barrier->ba_batches;
				for (i = 0; i < count; i++) {
					sum += lm_barrier->ba_data[i];
				}
				int mean = (int)round((double)sum/count);
				if (mean < (1000 * 1000)) {
					lm_optB = (int)((1000 * 1000) / mean);
				}
				else {
					lm_optB = 1;
				}
			}
			// Once the default thread joins here they can all continue.
			retb = barrier_queue(lm_barrier, NULL);
			if (retb < 0) {
				return (void *)1;
			}
		}
	} while (0 == retb);

	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): calling benchmark_finiworker()\n", getpid(), pthread_self());
	ret = benchmark_finiworker(arg);
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(%d, %0#lx): benchmark_finiworker() returned %d\n", getpid(), pthread_self(), ret);

	return ((r.re_errors + ret) > 0) ? (void *)1 : NULL;
}

static int
worker_process(void)
{
	int		i, ret, ret_val;
	void   *tsd;

	tids[0] = lm_default_thread = pthread_self();

	for (i = 1; i < lm_optT; i++) {
		tsd = gettsd(pindex, i);
		ret = pthread_create(&tids[i], NULL, worker_thread, tsd);
		if (ret != 0) {
			fprintf(stderr, "worker_process(%d): pthread_create(%p, NULL, %p, %p) failed: (%d) %s\n", getpid(), &tids[i], worker_thread, tsd, ret, strerror(ret));
			exit(1);
		}
	}

	tsd = gettsd(pindex, 0);
	ret_val = (int)(long long)worker_thread(tsd);

	for (i = 1; i < lm_optT; i++) {
		void *p_val = NULL;
		ret = pthread_join(tids[i], &p_val);
		if (ret != 0) {
			fprintf(stderr, "worker_process(%d): pthread_join(%p, NULL) failed: (%d) %s\n", getpid(), tids[i], ret, strerror(ret));
			exit(1);
		}
		if ((0 == ret_val) && p_val) ret_val = (int)(long long)p_val;
	}

	return ret_val;
}

void
usage(void)
{
	unsigned int len = strlen(lm_usage);
	(void) printf("Usage: %s\n"
			"\t---- All Benchmarks ----\n"
			"\t[-?] (print usage and exit)\n"
			"\t[-1] (single process; overrides -P > 1)\n"
			"\t[-A] (align with clock)\n"
			"\t[-B batch-size (default is calculated)]\n"
			"\t[-C minimum number of samples (default %d)]\n"
			"\t[-D minimum duration in ms (default %dms)]\n"
			"\t[-E] (echo name to stderr)\n"
			"\t[-G framework debugging level]\n"
			"\t[-H] (suppress headers)\n"
			"\t[-I nsecs per op] (used to compute batch size)\n"
			"\t[-L] (print argument line)\n"
			"\t[-M] (reports mean rather than median)\n"
			"\t[-N test-name (default '%s')]\n"
			"\t[-O getnsecs overhead]\n"
			"\t[-P processes (default %d)]\n"
			"\t[-R getnsecs resolution]\n"
			"\t[-S] (print detailed stats)\n"
			"\t[-T threads (default %d)]\n"
			"\t[-V] (print the libMicro version and exit)\n"
			"\t[-W] (flag possible benchmark problems, implies -S)\n"
			"\t[-X maximum duration in ms (default %dms)]\n"
			"%s%s%s",
			lm_procname,
			lm_defC, lm_defD, lm_procname, lm_defP, lm_defT, lm_defX,
			(len > 0
					? "\t---- Benchmark Specific ----\n"
					: ""),
			lm_usage,
			(len > 0
					? ((lm_usage[len-1] == '\n')
							? ""
							: "\n")
					: "\n"));
}

#define WARNING_INDENT	5

void
print_warnings(barrier_t *b)
{
	int head = 0;
	long long increase;

	if (b->ba_quant) {
		if (!head++) {
			(void) printf("#\n# WARNINGS\n");
		}
		assert(nsecs_resolution > 0);
		assert(lm_optB > 0);
		long long median = b->ba_corrected.st_median;
		if (median <= 0) median = 1;
		increase = (long long)(floor((nsecs_resolution * 100.0) /
			((double)lm_optB * median * 1000.0)) +
			1.0);
		(void) printf("#%*sQuantization error likely; "
				"perhaps increasing batch size (-B option, "
				"currently %d) %lldX will avoid this.\n",
				WARNING_INDENT, "", lm_optB, increase);
	}

	if (b->ba_batches < DEF_SAMPLES) {
		if (!head++) {
			(void) printf("#\n# WARNINGS\n");
		}

		(void) printf("#%*sToo few samples, %d < %d, "
				"consider running test longer, "
				"or for a least %d samples\n",
				WARNING_INDENT, "",
				b->ba_batches, DEF_SAMPLES, DEF_SAMPLES);
	}

	if ((lm_optM == 0)
			&& abs(b->ba_corrected.st_mean - b->ba_corrected.st_median)
					> (b->ba_corrected.st_stddev / 2)) {
		if (!head++) {
			(void) printf("#\n# WARNINGS\n");
		}

		printf("#%*sMean and median differ by more than one-half the standard deviation.\n", WARNING_INDENT, "");
	}

	if (b->ba_killed) {
		if (!head++) {
			(void) printf("#\n# WARNINGS\n");
		}

		if (b->ba_killed == KILLED_LONG) {
			printf("#%*sRan too long\n", WARNING_INDENT, "");
		}
		else {
			assert(b->ba_killed == KILLED_INT);
			printf("#%*sInterrupted\n", WARNING_INDENT, "");
		}
	}

	if ((lm_optT * lm_optP) == 1) {
		long long	elapsed_time = b->ba_endtime - b->ba_starttime;
		long long	run_time = b->ba_totaltime;
		double		percentage = 100.0 - (((double)(elapsed_time - run_time) / elapsed_time) * 100.0);
		if (percentage < 80.0) {
			if (!head++) {
				(void) printf("#\n# WARNINGS\n");
			}
			(void) printf("#%*sActual benchmark run time only accounts for %.1f%%"
					" of elapsed time.\n", WARNING_INDENT, "", percentage);
		}
	}

	if (b->ba_errors) {
		if (!head++) {
			(void) printf("#\n# WARNINGS\n");
		}
		(void) printf("#%*sErrors occured during benchmark.\n", WARNING_INDENT, "");
	}
}

#define STATS_FORMAT_L	"# %*s %*lld %*s %*lld"
#define STATS_FORMAT	"# %*s %*.*f %*s %*.*f"
#define STATS_1COLW		26
#define STATS_2COLW_L	 9
#define STATS_2COLW		15
#define STATS_3COLW_L	 9
#define STATS_3COLW		15
#define STATS_PREC		 5
#define STATS_SEPW_L	16
#define STATS_SEPW		10

static void
print_stats(barrier_t *b)
{
	if (b->ba_count == 0) {
		return;
	}

	(void) printf("#\n");
	(void) printf("# %-*s %*s %-*s %*s %s\n",
			STATS_1COLW, "STATISTICS",
			STATS_2COLW, "nsecs/call",
			STATS_SEPW, "(raw)",
			STATS_3COLW, "nsecs/call",
			"(outliers removed)");

	(void) printf(STATS_FORMAT_L "\n", STATS_1COLW, "min",
			STATS_2COLW_L, b->ba_raw.st_min,
			STATS_SEPW_L, "",
			STATS_3COLW_L, b->ba_corrected.st_min);
	(void) printf(STATS_FORMAT_L "\n", STATS_1COLW, "max",
			STATS_2COLW_L, b->ba_raw.st_max,
			STATS_SEPW_L, "",
			STATS_3COLW_L, b->ba_corrected.st_max);
	(void) printf(STATS_FORMAT "%s\n", STATS_1COLW, "mean",
			STATS_2COLW, STATS_PREC, b->ba_raw.st_mean,
			STATS_SEPW, "",
			STATS_3COLW, STATS_PREC, b->ba_corrected.st_mean,
			lm_optM?"*":"");
	(void) printf(STATS_FORMAT_L "%s\n", STATS_1COLW, "median",
			STATS_2COLW_L, b->ba_raw.st_median,
			STATS_SEPW_L, "",
			STATS_3COLW_L, b->ba_corrected.st_median,
			lm_optM?"":"*");
	(void) printf(STATS_FORMAT "\n", STATS_1COLW, "stddev",
			STATS_2COLW, STATS_PREC, b->ba_raw.st_stddev,
			STATS_SEPW, "",
			STATS_3COLW, STATS_PREC, b->ba_corrected.st_stddev);
	(void) printf(STATS_FORMAT "\n", STATS_1COLW, "standard error",
			STATS_2COLW, STATS_PREC, b->ba_raw.st_stderr,
			STATS_SEPW, "",
			STATS_3COLW, STATS_PREC, b->ba_corrected.st_stderr);
	(void) printf(STATS_FORMAT "\n", STATS_1COLW, "99% confidence level",
			STATS_2COLW, STATS_PREC, b->ba_raw.st_99confidence,
			STATS_SEPW, "",
			STATS_3COLW, STATS_PREC, b->ba_corrected.st_99confidence);
	(void) printf(STATS_FORMAT "\n", STATS_1COLW, "skew",
			STATS_2COLW, STATS_PREC, b->ba_raw.st_skew,
			STATS_SEPW, "",
			STATS_3COLW, STATS_PREC, b->ba_corrected.st_skew);
	(void) printf(STATS_FORMAT "\n", STATS_1COLW, "kurtosis",
			STATS_2COLW, STATS_PREC, b->ba_raw.st_kurtosis,
			STATS_SEPW, "",
			STATS_3COLW, STATS_PREC, b->ba_corrected.st_kurtosis);
	(void) printf(STATS_FORMAT "\n", STATS_1COLW, "time correlation",
			STATS_2COLW, STATS_PREC, b->ba_raw.st_timecorr,
			STATS_SEPW, "",
			STATS_3COLW, STATS_PREC, b->ba_corrected.st_timecorr);

	(void) printf("#\n# %*s %*.*lf\n",
			STATS_1COLW, "elapsed time",
			STATS_2COLW, STATS_PREC, (double)(b->ba_endtime - b->ba_starttime) / 1.0e9);
	if ((lm_optT * lm_optP) == 1) {
		(void) printf("# %*s %*.*lf\n",
				STATS_1COLW, "run time",
				STATS_2COLW, STATS_PREC, (double)b->ba_totaltime / 1.0e9);
	}
	(void) printf("# %*s %*u\n#\n", STATS_1COLW, "getnsecs overhead",
			STATS_2COLW_L, nsecs_overhead);

	if ((lm_optT * lm_optP) > 1) {
		(void) printf("# %*s %*d (%d per thread)\n", STATS_1COLW, "number of samples",
				STATS_2COLW_L, b->ba_batches, (b->ba_batches / (lm_optT * lm_optP)));
	}
	else {
		(void) printf("# %*s %*d\n", STATS_1COLW, "number of samples",
				STATS_2COLW_L, b->ba_batches);
	}
	if (b->ba_batches > b->ba_datasize)
		(void) printf("# %*s %*d (%d samples dropped)\n",
				STATS_1COLW, "number of samples retained",
				STATS_2COLW_L, b->ba_datasize, (b->ba_batches - b->ba_datasize));
	(void) printf("# %*s %*d\n", STATS_1COLW, "number of outliers",
			STATS_2COLW_L, b->ba_outliers);
	(void) printf("# %*s %*d\n", STATS_1COLW, "number of final samples",
			STATS_2COLW_L, b->ba_batches_final);

	if (!lm_dynamic_optB) {
		double usedB = (double)b->ba_count / b->ba_batches;
		(void) printf("# %*s %*.*lf (-B %d)\n", STATS_1COLW, "ops per sample",
				STATS_2COLW, STATS_PREC, usedB, lm_optB);
		int recB;
		if (b->ba_corrected.st_mean <= (1000 * 1000)) {
			/*
			 * Try to keep the batch size per timed run roughly 1 ms. Basically we
			 * don't completely trust that the timing measurements are accurate
			 * under 1 ms, and we don't want to track millions of samples, so we
			 * recommend keeping the batch size just large enough to fit in 1 ms.
			 */
			recB = (int)round((1000 * 1000) / b->ba_corrected.st_mean);
		}
		else {
			recB = 1;
		}
		if ((abs(usedB - recB) / (double)usedB) > 0.2)
			(void) printf("#\n# %*s %*d\n", STATS_1COLW, "recommended -B value",
					STATS_2COLW_L, recB);
	}

	print_histo(b);

	if (lm_optW) {
		print_warnings(b);
	}
}

void
update_stats(barrier_t *b, result_t *r)
{
	long long	time;
	long long	nsecs_per_call;

	b->ba_count	 += r->re_count;
	b->ba_errors += r->re_errors;

	b->ba_totaltime += (time = (r->re_t1 - r->re_t0));
	time -= nsecs_overhead;
	if (time < (100 * nsecs_resolution))
		b->ba_quant++;

	nsecs_per_call = (long long)round((time / (double)r->re_count));
	b->ba_data[b->ba_batches % b->ba_datasize] = nsecs_per_call;

	b->ba_batches++;
}

#if defined(__FreeBSD__) && !defined(USE_SEMOP)
# error "Use of SEMOP barriers required on FreeBSD"
#endif

barrier_t *
barrier_create(int hwm, int datasize)
{
	if (datasize / hwm < MIN_DATASIZE)
		datasize = MIN_DATASIZE * hwm;
	unsigned int size = sizeof (barrier_t)
			+ ((datasize - 1) * sizeof (*((barrier_t *)NULL)->ba_data));
	/*LINTED*/
	barrier_t	*b = (barrier_t *)mmap(NULL, size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0L);
	if ((barrier_t *)MAP_FAILED == b) {
		perror("barrier_create(): mmap()");
		return NULL;
	}

	b->ba_size = size;
	b->ba_datasize = datasize;
	b->ba_hwm = hwm;

#ifdef USE_SEMOP
	b->ba_semid = semget(IPC_PRIVATE, 3, 0600);
	if (b->ba_semid == -1) {
		perror("barrier_create(): semget()");
		(void) munmap((void *)b, sizeof (barrier_t));
		return NULL;
	}

	struct sembuf		s[1];

	/* [hwm - 1, 0, 0] */
	s[0].sem_num = 0;
	s[0].sem_op	 = hwm - 1;
	s[0].sem_flg = 0;
	if (semop(b->ba_semid, s, 1) == -1) {
		perror("barrier_create(): semop(1)");
		(void) semctl(b->ba_semid, 0, IPC_RMID);
		(void) munmap((void *)b, sizeof (barrier_t));
		return NULL;
	}
#else
	pthread_mutexattr_t	mattr;
	int ret = pthread_mutexattr_init(&mattr);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_mutexattr_init(%p) failed: (%d) %s\n", &mattr, ret, strerror(ret));
		return NULL;
	}
	ret = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_mutexattr_setpshared(%p, %d) failed: (%d) %s\n", &mattr, PTHREAD_PROCESS_SHARED, ret, strerror(ret));
		return NULL;
	}

	pthread_condattr_t	cattr;
	ret = pthread_condattr_init(&cattr);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_condattr_init(%p) failed: (%d) %s\n", &cattr, ret, strerror(ret));
		return NULL;
	}
	ret = pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_condattr_setpshared(%p, %d) failed: (%d) %s\n", &cattr, PTHREAD_PROCESS_SHARED, ret, strerror(ret));
		return NULL;
	}

	ret = pthread_mutex_init(&b->ba_lock, &mattr);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_mutex_init(%p, %p) failed: (%d) %s\n", &b->ba_lock, &mattr, ret, strerror(ret));
		return NULL;
	}
	ret = pthread_cond_init(&b->ba_cv, &cattr);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_cond_init(%p, %p) failed: (%d) %s\n", &b->ba_cv, &cattr, ret, strerror(ret));
		return NULL;
	}
#endif

	b->ba_waiters = 0;
	b->ba_phase = 0;

	b->ba_count = 0;
	b->ba_errors = 0;

	b->ba_starttime = 0;
	b->ba_minruntime = 0;
	b->ba_deadline = 0;

	return b;
}

int
barrier_destroy(barrier_t *b)
{
	int				ret, ret_val = 0;
	unsigned int	size = b->ba_size;

#ifdef USE_SEMOP
	ret = semctl(b->ba_semid, 0, IPC_RMID);
	if (ret < 0) {
		perror("barrier_destroy(): semctl(IPC_RMID)");
		ret_val = 1;
	}
#endif
	ret = munmap((void *)b, b->ba_size);
	if (ret < 0) {
		perror("barrier_destroy(): munmap()");
		ret_val = 1;
	}

	return ret_val;
}

int
barrier_queue(barrier_t *b, result_t *r)
{
#ifdef USE_SEMOP
	struct sembuf		s[2];

	/*
	 * {s0(-(hwm-1))}
	 * if ! nowait {s1(-(hwm-1))}
	 *	 (all other threads)
	 *	 update shared stats
	 *	 {s0(hwm-1), s1(1)}
	 *	 {s0(1), s2(-1)}
	 * else
	 *	 (last thread)
	 *	 update shared stats
	 *	 {s2(hwm-1)}
	 */

	s[0].sem_num = 0;
	s[0].sem_op	 = -(b->ba_hwm - 1);
	s[0].sem_flg = 0;
	if (semop(b->ba_semid, s, 1) == -1) {
		perror("semop(2)");
		return -1;
	}

	s[0].sem_num = 1;
	s[0].sem_op	 = -(b->ba_hwm - 1);
	s[0].sem_flg = IPC_NOWAIT;
	if (semop(b->ba_semid, s, 1) == -1) {
		if (errno != EAGAIN) {
			perror("semop(3)");
			return -1;
		}

		/* all but the last thread */

		if (r != NULL) {
			update_stats(b, r);
		}

		b->ba_waiters++;

		s[0].sem_num = 0;
		s[0].sem_op	 = b->ba_hwm - 1;
		s[0].sem_flg = 0;
		s[1].sem_num = 1;
		s[1].sem_op	 = 1;
		s[1].sem_flg = 0;
		if (semop(b->ba_semid, s, 2) == -1) {
			perror("semop(4)");
			return -1;
		}

		s[0].sem_num = 0;
		s[0].sem_op	 = 1;
		s[0].sem_flg = 0;
		s[1].sem_num = 2;
		s[1].sem_op	 = -1;
		s[1].sem_flg = 0;
		if (semop(b->ba_semid, s, 2) == -1) {
			perror("semop(5)");
			return -1;
		}

	} else {
		/* the last thread */

		if (r != NULL) {
			update_stats(b, r);
		}

		b->ba_waiters = 0;
		b->ba_phase++;

		s[0].sem_num = 2;
		s[0].sem_op	 = b->ba_hwm - 1;
		s[0].sem_flg = 0;
		if (semop(b->ba_semid, s, 1) == -1) {
			perror("semop(6)");
			return -1;
		}
	}
#else /* USE_SEMOP */
	int	phase;

	int ret = pthread_mutex_lock(&b->ba_lock);
	if (ret != 0) {
		fprintf(stderr, "barrier_queue(): pthread_mutex_lock(%p) failed: (%d) %s\n", &b->ba_lock, ret, strerror(ret));
		return -1;
	}

	if (NULL != r) {
		update_stats(b, r);
	}

	phase = b->ba_phase;

	if (phase >= 0) {
		b->ba_waiters++;
		if (b->ba_hwm == b->ba_waiters) {
			b->ba_waiters = 0;
			/* time to stop? */
			if ((NULL != r) && ((b->ba_deadline > 0) || (b->ba_minruntime > 0))) {
				long long curtim = getnsecs();
				if (((b->ba_deadline > 0) && (curtim > b->ba_deadline))
						|| ((b->ba_batches >= (lm_optC * lm_optT * lm_optP))
								&& ((b->ba_minruntime > 0) && (curtim > b->ba_minruntime)))) {
					b->ba_phase = -1;
				}
				else {
					b->ba_phase++;
				}
			}
			else {
				b->ba_phase++;
			}
			ret = pthread_cond_broadcast(&b->ba_cv);
			if (ret != 0) {
				fprintf(stderr, "barrier_queue(): pthread_cond_broadcast(%p) failed: (%d) %s\n", &b->ba_cv, ret, strerror(ret));
				return -1;
			}
		}

		while (b->ba_phase == phase) {
			ret = pthread_cond_wait(&b->ba_cv, &b->ba_lock);
			if (ret != 0) {
				fprintf(stderr, "barrier_queue(): pthread_cond_wait(%p, %p) failed: (%d) %s\n", &b->ba_cv, &b->ba_lock, ret, strerror(ret));
				return -1;
			}
		}
	}

	// Check to see if the final batch has been reached.
	int ret_val = (b->ba_phase < 0) ? 1 : 0;

	ret = pthread_mutex_unlock(&b->ba_lock);
	if (ret != 0) {
		fprintf(stderr, "barrier_queue(): pthread_mutex_unlock(%p) failed: (%d) %s\n", &b->ba_lock, ret, strerror(ret));
		return -1;
	}
#endif /* USE_SEMOP */

	return ret_val;
}

int
gettindex(void)
{
	int	i;

	assert (NULL != tids);

	for (i = 0; i < lm_optT; i++) {
		if (pthread_self() == tids[i])
			break;
	}

	return (pindex * lm_optT) + i;
}

static void *
gettsd(int p, int t)
{
	if ((p < 0) || (p >= lm_optP) || (t < 0) || (t >= lm_optT))
		return NULL;

	return ((void *)((unsigned long)tsdseg +
		(((p * lm_optT) + t) * tsdsize)));
}

#ifdef USE_GETHRTIME
long long
getnsecs(void)
{
	return gethrtime();
}

long long
getusecs(void)
{
	return gethrtime() / 1000;
}

#elif USE_RDTSC /* USE_GETHRTIME */

__inline__ long long
rdtsc(void)
{
	unsigned long long x;
	__asm__ volatile(".byte 0x0f, 0x31" : "=A" (x));
	return x;
}

long long
getusecs(void)
{
	return (rdtsc() * 1000000LL) / lm_hz;
}

long long
getnsecs(void)
{
	return (rdtsc() * 1000000000LL) / lm_hz;
}

#elif USE_GTOD /* USE_GETHRTIME */

long long
getusecs(void)
{
	struct timeval		tv;

	(void) gettimeofday(&tv, NULL);

	return ((long long)tv.tv_sec * 1000000LL) + (long long)tv.tv_usec;
}

long long
getnsecs(void)
{
	struct timeval		tv;

	(void) gettimeofday(&tv, NULL);

	return ((long long)tv.tv_sec * 1000000000LL) + ((long long)tv.tv_usec * 1000LL);
}

#else /* USE_GETHRTIME */

long long
getusecs(void)
{
	struct timespec		ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);

	return ((long long)ts.tv_sec * 1000000LL) + ((long long)ts.tv_nsec / 1000LL);
}

long long
getnsecs(void)
{
	struct timespec		ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);

	return ((long long)ts.tv_sec * 1000000000LL) + (long long)ts.tv_nsec;
}

#endif /* USE_GETHRTIME */

void
setfdlimit(int limit)
{
	struct rlimit rlimit;

	if (getrlimit(RLIMIT_NOFILE, &rlimit) < 0) {
		perror("getrlimit");
		return;
	}

	if (rlimit.rlim_cur > limit)
		return; /* no worries */

	rlimit.rlim_cur = limit;

	if (rlimit.rlim_max < limit)
		rlimit.rlim_max = limit;

	if (setrlimit(RLIMIT_NOFILE, &rlimit) < 0) {
		perror("setrlimit");
	}
}

#define	KILOBYTE		1024
#define	MEGABYTE		(KILOBYTE * KILOBYTE)
#define	GIGABYTE		(KILOBYTE * MEGABYTE)

long long
sizetoll(const char *arg)
{
	int			len = strlen(arg);
	int			i;
	long long		mult = 1;

	if (len && isalpha(arg[len - 1])) {
		switch (arg[len - 1]) {

		case 'k':
		case 'K':
			mult = KILOBYTE;
			break;
		case 'm':
		case 'M':
			mult = MEGABYTE;
			break;
		case 'g':
		case 'G':
			mult = GIGABYTE;
			break;
		default:
			return -1;
		}

		for (i = 0; i < len - 1; i++)
			if (!isdigit(arg[i]))
				return -1;
	}

	return (mult * strtoll(arg, NULL, 10));
}

int
sizetoint(const char *arg)
{
	int			len = strlen(arg);
	int			i;
	long long		mult = 1;

	if (len && isalpha(arg[len - 1])) {
		switch (arg[len - 1]) {

		case 'k':
		case 'K':
			mult = KILOBYTE;
			break;
		case 'm':
		case 'M':
			mult = MEGABYTE;
			break;
		case 'g':
		case 'G':
			mult = GIGABYTE;
			break;
		default:
			return -1;
		}

		for (i = 0; i < len - 1; i++)
			if (!isdigit(arg[i]))
				return -1;
	}

	return (mult * atoi(arg));
}

static void
print_bar(long count, long total, unsigned int width)
{
	int			i;

	(void) putchar_unlocked(count ? '*' : ' ');
	for (i = 1; i < (width * count) / total; i++)
		(void) putchar_unlocked('*');
	for (; i < width; i++)
		(void) putchar_unlocked(' ');
}

static int
longlongcmp(const void *p1, const void *p2)
{
	long long a = *((long long *)p1);
	long long b = *((long long *)p2);

	if (a > b)
		return 1;
	if (a < b)
		return -1;
	return 0;
}

#define HISTO_INDENT	7
#define HISTO_COL1W	12
#define HISTO_COL2W	15
#define HISTO_COL3W	15
#define HISTO_BARW	32U
#define HISTO_PREC	5

static void
print_histo(barrier_t *b)
{
	int			n;
	int			i;
	int			idx;
	int			last;
	long long	maxcount;
	long long	sum;
	long long	min;
	long long	bucket_width;
	long long	count;
	int			i95;	// Index of 95%ile element
	long long	v95;	// Value of 95%ile element
	long long	r95;	// Range of values in 95%ile
	double		m95;	// Mean of 95%itle values
	histo_t	   *histo;

	/* create and initialise the histogram */
	histo = calloc(sizeof (histo_t), HISTOSIZE);
	if (NULL == histo) {
		printf("#%*sNo available memory for histogram.\n", HISTO_INDENT, "");
		return;
	}

	/* calculate how much data we've captured */
	n = b->ba_batches_final;

	/* find the 95th percentile - index, value and range */

	/* Skip over any infinity or NaN results */
	for (i95 = ((n * 95) / 100); (i95 > 0); i95--) {
		v95 = b->ba_data[i95];
		if (v95 > 0) {
			break;
		}
	}

	if (i95 < 0) {
		printf("#%*sNo valid data present for histogram.\n", HISTO_INDENT, "");
		return;
	}

	min = b->ba_data[0];
	r95 = (v95 - min) + 1;

	/* find a suitable min and scale */
	bucket_width = (long long)ceil((double)r95 / HISTOSIZE);

	/* populate the histogram */
	last = 0;
	sum = 0;
	count = 0;
	for (i = 0; i < i95; i++) {
		idx = (int)((b->ba_data[i] - min) / bucket_width);
		if (idx >= HISTOSIZE) {
			if (idx > HISTOSIZE)
				(void) printf("%*s** panic! ** invalid bucket index\n", HISTO_INDENT, "");
			idx = HISTOSIZE - 1;
		}

		histo[idx].sum += b->ba_data[i];
		histo[idx].count++;

		sum += b->ba_data[i];
		count++;
	}
	m95 = (double)sum / count;

	/* find the largest bucket */
	maxcount = 0;
	for (i = 0; i < HISTOSIZE; i++)
		if (histo[i].count > 0) {
			last = i;
			if (histo[i].count > maxcount)
				maxcount = histo[i].count;
		}
	if (maxcount < 1) {
		return;
	}

	(void) printf("#\n");
	(void) printf("# DISTRIBUTION\n");

	(void) printf("#%*s%*s %*s %*s %*s\n", HISTO_INDENT, "",
			HISTO_COL1W, "counts", HISTO_COL2W, "nsecs/call",
			HISTO_BARW, "", HISTO_COL3W, "means");

	/* print the buckets */
	for (i = 0; i <= last; i++) {
		(void) printf("#%*s%*lld %*lld |", HISTO_INDENT, "",
				HISTO_COL1W, histo[i].count,
				HISTO_COL2W, min + (bucket_width * i));

		print_bar(histo[i].count, maxcount, HISTO_BARW);

		if ((histo[i].count > 0) && (bucket_width > 1))
			(void) printf("%*.*f\n",
					HISTO_COL3W, HISTO_PREC, (double)histo[i].sum / histo[i].count);
		else
			(void) printf("%*s\n", HISTO_COL3W, "-");
	}

	/* find the mean of values beyond the 95th percentile */
	sum = 0;
	count = 0;
	for (i = i95; i < n; i++) {
		sum += b->ba_data[i];
		count++;
	}

	/* print the >95% bucket summary */
	(void) printf("#\n");
	(void) printf("#%*s%*lld %*s |", HISTO_INDENT, "", HISTO_COL1W, count,
			HISTO_COL2W, "> 95%");
	print_bar(count, maxcount, HISTO_BARW);
	if (count > 0)
		(void) printf("%*.*f\n", HISTO_COL3W, HISTO_PREC, (double)sum / count);
	else
		(void) printf("%*s\n", HISTO_COL3W, "-");
	(void) printf("#\n");
	(void) printf("#%*s%*s %*.*f\n", HISTO_INDENT, "",
			HISTO_COL1W, "mean of 95%", HISTO_COL2W, HISTO_PREC, m95);
	(void) printf("#%*s%*s %*lld\n", HISTO_INDENT, "",
			HISTO_COL1W, "95th %ile", HISTO_COL2W, v95);
}

static void
compute_stats(barrier_t *b)
{
	int i, batches;

	batches = (b->ba_batches >= b->ba_datasize)
			? b->ba_datasize
			: b->ba_batches;

	/*
	 * do raw stats
	 */

	qsort((void *)b->ba_data, batches, sizeof (*b->ba_data), longlongcmp);
	crunch_stats(b->ba_data, batches, &b->ba_raw);

	/*
	 * recursively apply 3 sigma rule to remove outliers
	 */

	b->ba_corrected = b->ba_raw;
	b->ba_outliers = 0;

	if (batches > 40) { /* remove outliers */
		int removed;

		do {
			removed = remove_outliers(b->ba_data, batches, &b->ba_corrected);
			b->ba_outliers += removed;
			batches -= removed;
			crunch_stats(b->ba_data, batches, &b->ba_corrected);
		} while (removed != 0 && batches > 40);
	}
	b->ba_batches_final = batches;

	if (b->ba_count == 0) {
		b->ba_errors++;
	}
}

/*
 * routine to compute various statistics on array of doubles (previously sorted).
 */

static void
crunch_stats(long long *data, int count, stats_t *stats)
{
	double		a;
	double		std;
	double		diff;
	double		sk;
	double		ku;
	double		mean;
	long long	sum;
	int			i;
	long long  *xdata;

	/*
	 * first we need the mean
	 */

	sum = 0;
	for (i = 0; i < count; i++) {
		sum += data[i];
	}

	stats->st_mean = mean = ((double)sum / count);
	stats->st_median = data[count/2];

	fit_line(NULL, data, count, &a, &stats->st_timecorr);

	std = 0.0;
	sk	= 0.0;
	ku	= 0.0;

	stats->st_max = -1;
	stats->st_min = LLONG_MAX;

	for (i = 0; i < count; i++) {
		if (data[i] > stats->st_max)
			stats->st_max = data[i];
		if (data[i] < stats->st_min)
			stats->st_min = data[i];

		diff = data[i] - mean;
		double diff2 = diff * diff;
		std	+= diff2;
		double diff3 = diff2 * diff;
		sk	+= diff3;
		ku	+= diff3 * diff;
	}

	double cm1		   = (double)(count - 1);
	stats->st_stddev   = std = sqrt(std/cm1);
	stats->st_stderr   = std / sqrt(count);
	stats->st_99confidence = stats->st_stderr * 2.576;
	double std3		   = (std * std * std);
	stats->st_skew	   = (sk / (cm1 * std3));
	stats->st_kurtosis = (ku / (cm1 * (std3 * std))) - 3;
}

/*
 * does a least squares fit to the set of points x, y and
 * fits a line y = a + bx. Returns a, b
 */

static void
fit_line(long long *x, long long *y, int count, double *a, double *b)
{
	double sumx, sumy, sumxy, sumx2;
	double denom;
	int i;

	sumx = sumy = sumxy = sumx2 = 0.0;

	for (i = 0; i < count; i++) {
		long long x_sub_i = (NULL == x) ? i : x[i];
		sumx	+= x_sub_i;
		sumx2	+= x_sub_i * x_sub_i;
		long long y_sub_i = y[i];
		sumy	+= y_sub_i;
		sumxy	+= x_sub_i * y_sub_i;
	}

	denom = (count * sumx2) - (sumx * sumx);

	if (denom == 0.0) {
		*a = *b = NAN;
	}
	else {
		*a = ((sumy * sumx2) - (sumx * sumxy)) / denom;
		*b = ((count * sumxy) - (sumx * sumy)) / denom;
	}
}

/*
 * empty function for measurement purposes
 */

int
nop(void)
{
	return 1;
}

#define	NSECITER	1000*1000*20

unsigned int
get_nsecs_overhead(void)
{
	long long s;

	long long *data = calloc(sizeof(long long), NSECITER);
	if (NULL == data) {
		perror("get_nsecs_overhead: calloc()");
		return 0;
	}

	stats_t stats;

	int i;
	int count;
	int outliers;

	(void) getnsecs(); /* warmup */
	(void) getnsecs(); /* warmup */
	(void) getnsecs(); /* warmup */

	i = 0;

	count = NSECITER;

	for (i = 0; i < count; i++) {
		s = getnsecs();
		data[i] = getnsecs() - s;
	}

	qsort((void *)data, count, sizeof (long long), longlongcmp);
	crunch_stats(data, count, &stats);

	while ((outliers = remove_outliers(data, count, &stats)) != 0) {
		count -= outliers;
		crunch_stats(data, count, &stats);
	}

	free(data);
	return (unsigned int)round(stats.st_mean);
}

/*
 * Determine the resolution of the system's high resolution counter. Most
 * hardware has a nanosecond resolution counter, but some systems still use
 * course resolution (e.g. derived instead by a periodic interrupt).
 *
 * Algorithm:
 *
 * Determine a busy loop that is long enough for successive nanosecond counter
 * reads to report different times, called nops. Then take RES_SAMPLES samples
 * with busy loop interval successively increases by multiples of nops. The
 * counter resolution is assumed to be the smallest non-zero time delta
 * between these samples.
 *
 * One last wrinkle is all samples may have the same delta on a system with a
 * very fast and consistent hardware counter based getnsecs(). In that case
 * assume the resolution is 1ns.
 */
#define RES_SAMPLES	10000
unsigned int
get_nsecs_resolution(void)
{
	long long *y;

	volatile int i, j;
	int nops;
	unsigned int res;
	long long start, stop;

	y = calloc(sizeof(*y), RES_SAMPLES);
	if (y == NULL) {
		perror("get_nsecs_resolution: calloc");
		return 1;
	}

	/*
	 * first, figure out how many nops to use to get any delta between time
	 * measurements, using a minimum of one.
	 */

	/*
	 * warm cache
	 */

	stop = start = getnsecs();
	int maxiter = 1000;
	do {
		for (i = 1; i <= maxiter; i++) {
			start = getnsecs();
			for (j = i; j; j--)
				;
			stop = getnsecs();
			if (stop > start)
				break;
		}
		if (stop <= start)
			maxiter *= 10;
	} while (stop <= start);

	nops = i;

	/*
	 * now collect data at linearly varying intervals
	 */

	for (i = 0; i < RES_SAMPLES; i++) {
		start = getnsecs();
		for (j = nops * i; j; j--)
			;
		stop = getnsecs();
		y[i] = stop - start;
	}

	/*
	 * find smallest positive difference between samples; this is the counter
	 * resolution
	 */

	res = y[0];
	for (i = 1; i < RES_SAMPLES; i++) {
		int diff = y[i] - y[i-1];

		if (diff > 0 && res > diff)
			res = diff;
	}

	if (res == 0) {
		// All differences are dead on, assume 1 nanosecond resolution.
		res = 1;
	}

	free(y);
	return res;
}

/*
 * remove any data points from the array more than 3 sigma out; assumes the
 * data is already sorted.
 */

static int
remove_outliers(long long *data, int count, stats_t *stats)
{
	long long outmin = (long long)round(stats->st_mean - 3 * stats->st_stddev);
	long long outmax = (long long)round(stats->st_mean + 3 * stats->st_stddev);

	int i, outliers;

	int min_idx = count;
	for (i = 0; i < count; i++) {
		if (data[i] >= outmin) {
			min_idx = i;
			break;
		}
	}

	int max_idx = -1;
	for (i = (count - 1); i >= 0; i--) {
		if (data[i] <= outmax) {
			max_idx = i;
			break;
		}
	}

	if (min_idx > 0) {
		int idx;
		for (idx = min_idx, i = 0; idx <= max_idx && i < count; idx++, i++) {
			data[i] = data[idx];
		}
		outliers = count - i;
	}
	else {
		outliers = count - (max_idx + 1);
	}

	return outliers;
}
