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

#define DEF_SAMPLES 100
#define DEF_TIME 10 /* seconds */
#define MAX_TIME 600 /* seconds, or 10 minutes */

/*
 * user visible globals
 */

int				lm_argc = 0;
char		  **lm_argv = NULL;

int				lm_opt1;
int				lm_optA;
int				lm_optB;
int				lm_optC = 0;
int				lm_optD;
int				lm_optE;
int				lm_optG = 0;
int				lm_optH;
int				lm_optI;
int				lm_optL = 0;
int				lm_optM = 0;
char		   *lm_optN;
int				lm_optP;
int				lm_optS;
int				lm_optT;
int				lm_optW;
int				lm_optX;

int				lm_def1 = 0;
int				lm_defB = 0; /* use lm_nsecs_per_op */
int				lm_defC = DEF_SAMPLES;
int				lm_defD = DEF_TIME*1000; /* DEF_TIME ms */
int				lm_defH = 0;
char		   *lm_defN = NULL;
int				lm_defP = 1;
int				lm_defS = 0;
int				lm_defT = 1;
int				lm_defX = MAX_TIME*1000; /* MAX_TIME ms */

/*
 * default on fast platform, should be overridden by individual
 * benchmarks if significantly wrong in either direction.
 */
int				lm_nsecs_per_op = 1000; /* 1,000 ns, or 1us */

char		   *lm_procpath;
char			lm_procname[STRSIZE];
char			lm_usage[STRSIZE];
char			lm_optstr[STRSIZE];
char			lm_header[STRSIZE];
size_t			lm_tsdsize = 0;


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

static void			worker_process();
static void			usage();
static void			print_stats(barrier_t *);
static void			print_histo(barrier_t *);
static int			remove_outliers(double *, int, stats_t *);
static long long	nsecs_overhead;
static long long	nsecs_resolution;
static long long	get_nsecs_overhead();
static int			crunch_stats(double *, int, stats_t *);
static void			compute_stats(barrier_t *);
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

#ifdef USE_RDTSC
	if (getenv("LIBMICRO_HZ") == NULL) {
		(void) printf("LIBMICRO_HZ needed but not set\n");
		exit(1);
	}
	lm_hz = strtoll(getenv("LIBMICRO_HZ"), NULL, 10);
#endif

	startnsecs = getnsecs();

	lm_argc = argc;
	lm_argv = argv;

	/* before we do anything */
	(void) benchmark_init();

	/* check that the case defines lm_tsdsize before proceeding */
	if (lm_tsdsize == (size_t)-1) {
		(void) fprintf(stderr, "error in benchmark_init: "
			"lm_tsdsize not set\n");
		exit(1);
	}

	nsecs_overhead = get_nsecs_overhead();
	nsecs_resolution = get_nsecs_resolution();

	/*
	 * Set defaults
	 */

	lm_opt1	= lm_def1;
	lm_optB	= lm_defB;
	lm_optD	= lm_defD;
	lm_optH	= lm_defH;
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

	(void) snprintf(optstr, sizeof(optstr), "1AB:C:D:EG:HI:LMN:P:ST:VWX:?%s", lm_optstr);
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
		case 'P':
			lm_optP = sizetoint(optarg);
			break;
		case 'S':
			lm_optS = 1;
			break;
		case 'T':
			lm_optT = sizetoint(optarg);
			break;
		case 'V':
			(void) printf("%s\n", LIBMICRO_VERSION);
			exit(0);
			break;
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
			exit(0);
			break;
		default:
			if (benchmark_optswitch(opt, optarg) == -1) {
				usage();
				exit(0);
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
		 * so use a computed value.
		 *
		 * In a DEF_TIME second period (see lm_optD above), we want to have
		 * DEF_SAMPLES samples executed in that period. So each batch size
		 * should run for about DEF_TIME/100 seconds.
		 */
		if (lm_optI)
			lm_nsecs_per_op = lm_optI;

		long long sample_time;
		if (lm_optC > 0) {
			/*
			 * We have a run limit set, so try to set the batch size to give
			 * us a run of DEF_TIME seconds total.
			 */
			sample_time = (long long)round((DEF_TIME * 1000 * 1000 * 1000LL) / (double)lm_optC);
		}
		else {
			assert(lm_optD > 0);
			/*
			 * We have a time limit, so divide it into DEF_SAMPLES samples,
			 * and set the batch size appropriately to fit in the sample time
			 * period.
			 */
			sample_time = (long long)round((lm_optD * 1000 * 1000LL) / (double)DEF_SAMPLES);
		}
		lm_optB = (int)(sample_time / lm_nsecs_per_op);

		if (lm_optB == 0) {
			if (lm_optG >= 1) fprintf(stderr, "DEBUG1 (%s): (sample_time (%lld) / lm_nsecs_per_op (%d)) == 0, defaulting lm_optB to one (1)\n", lm_optN, sample_time, lm_nsecs_per_op);
			lm_optB = 1;
		}
		else if (lm_optG >= 2) {
			fprintf(stderr, "DEBUG2 (%s): defaulting lm_optB to %d\n", lm_optN, lm_optB);
		}
	}

	if ((lm_optG >= 2) && (lm_optB < 20)) {
		fprintf(stderr, "DEBUG2 (%s): lm_optB = %d\n", lm_optN, lm_optB);
	}

	/*
	 * now that the options are set
	 */

	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main() calling benchmark_initrun()\n");
	ret = benchmark_initrun();
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main() benchmark_initrun() returned %d\n", ret);
	if (ret == -1) {
		exit(1);
	}

	/* allocate dynamic data */
	pids = (pid_t *)malloc(lm_optP * sizeof (pid_t));
	if (pids == NULL) {
		perror("malloc(pids)");
		exit(1);
	}
	tids = (pthread_t *)malloc(lm_optT * sizeof (pthread_t));
	if (tids == NULL) {
		perror("malloc(tids)");
		exit(1);
	}

	/* round up tsdsize to nearest 128 to eliminate false sharing */
	tsdsize = ((lm_tsdsize + 127) / 128) * 128;

	/* allocate sufficient TSD for each thread in each process */
	tsdseg = (void *)mmap(NULL, lm_optT * lm_optP * tsdsize + 8192,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0L);
	if (tsdseg == NULL) {
		perror("mmap(tsd)");
		exit(1);
	}

	/* initialise worker synchronisation */
	b = barrier_create(lm_optT * lm_optP, DATASIZE);
	if (b == NULL) {
		perror("barrier_create()");
		exit(1);
	}
	lm_barrier = b;
	b->ba_flag = 1;

	/* need this here so that parent and children can call exit() */
	(void) fflush(stdout);
	(void) fflush(stderr);

	/* when we started and when to stop */

	b->ba_starttime = getnsecs();
	b->ba_minruntime = (long long) (b->ba_starttime + (lm_optD * 1000000LL));

	if (lm_optX > 0)
		b->ba_deadline = (long long) (b->ba_starttime + (lm_optX * 1000000LL));
	else if (lm_optC <= 0)
		b->ba_deadline = b->ba_minruntime;
	else
		b->ba_deadline = 0;

	/* do the work */
	if (lm_opt1) {
		/* single process, non-fork mode */
		pindex = 0;
		worker_process();
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
			exit(1);
		}

		int sigfd = signalfd(-1, &mask, SFD_CLOEXEC);
		if (sigfd == -1) {
			perror("signalfd");
			exit(1);
		}

		/* create worker processes */
		for (i = 0; i < lm_optP; i++) {
			pids[i] = fork();

			switch (pids[i]) {
			case 0:
				pindex = i;
				worker_process();
				exit(0);
				break;
			case -1:
				perror("fork");
				exit(1);
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
				exit(1);
			}

			/* Kill the test if it goes one minute over the deadline */
			timeout.it_value.tv_sec = (b->ba_deadline / 1000000000LL) + 60;
			ret = timer_settime(host_timer, TIMER_ABSTIME, &timeout, NULL);
			if (ret < 0) {
				perror("timer_settime");
				exit(1);
			}
		}

		int done = 0;
		while (!done) {
			struct signalfd_siginfo fdsi;
			memset(&fdsi, 0, sizeof(struct signalfd_siginfo));
			ssize_t s = read(sigfd, &fdsi, sizeof(struct signalfd_siginfo));
			if (s != sizeof(struct signalfd_siginfo)) {
				perror("read(signalfd)");
				exit(1);
			}

			if (fdsi.ssi_signo == SIGALRM
					|| fdsi.ssi_signo == SIGINT) {
				if (fdsi.ssi_signo == SIGALRM) {
					b->ba_killed = KILLED_LONG;
				}
				else {
					b->ba_killed = KILLED_INT;
				}

				/* kill the worker processes */
				for (i = 0; i < lm_optP; i++) {
					if (pids[i] > 0) {
						int ret = kill(pids[i], SIGKILL);
						if (ret < 0) {
							if (errno != ESRCH) {
								perror("kill");
								exit(1);
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
							exit(1);
						}
						if (WIFEXITED(status) || WIFSIGNALED(status)) {
							pids[i] = 0;
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
		}

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
			(double)(getnsecs() - startnsecs) /
			1.e9);
		(void) fflush(stderr);
	}

	/* print result header (unless suppressed) */
	if (!lm_optH) {
		(void) printf("%*s %3s %3s %12s %12s %8s %8s %s\n",
				strlen(lm_optN), "", "prc", "thr",
				"usecs/call",
				"samples", "errors", "cnt/samp", lm_header);
	}

	/* print result */

	(void) printf("%-*s %3d %3d %12.5f %12d %8lld %8d %s\n",
			strlen(lm_optN), lm_optN, lm_optP, lm_optT,
			(lm_optM?b->ba_corrected.st_mean:b->ba_corrected.st_median),
			b->ba_batches_final, b->ba_errors, lm_optB,
			benchmark_result());

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
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): benchmark_finirun() returned %d\n", ret);
	(void) barrier_destroy(b);
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): calling benchmark_fini()\n");
	ret = benchmark_fini();
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: actual_main(): benchmark_fini() returned %d\n", ret);

	return 0;
}

void *
worker_thread(void *arg)
{
	result_t	r;
	long long	last_sleep = 0;
	long long	t;
	int			ret;

	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): calling benchmark_initworker()\n");
	r.re_errors = ret = benchmark_initworker(arg);
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): benchmark_initworker() returned %d\n", ret);

	while (lm_barrier->ba_flag) {
		r.re_count = 0;
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): calling benchmark_initbatch()\n");
		r.re_errors += ret = benchmark_initbatch(arg);
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): benchmark_initbatch() returned %d\n", ret);

		/* sync to clock */

		if (lm_optA && ((t = getnsecs()) - last_sleep) > 75000000LL) {
			(void) poll(0, 0, 10);
			last_sleep = t;
		}
		/* wait for it ... */
		(void) barrier_queue(lm_barrier, NULL);

		/* time the test */
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): calling benchmark()\n");
		r.re_t0 = getnsecs();
		ret = benchmark(arg, &r);
		r.re_t1 = getnsecs();
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): benchmark() returned %d\n", ret);

		/* record results and sync */
		(void) barrier_queue(lm_barrier, &r);

		/* time to stop? */
		if (((lm_barrier->ba_deadline > 0)
					&& (r.re_t1 > lm_barrier->ba_deadline))
				|| ((lm_barrier->ba_batches >= lm_optC)
						&& (r.re_t1 > lm_barrier->ba_minruntime))) {
			lm_barrier->ba_flag = 0;
		}

		/* Errors from finishing this batch feed into the next batch */
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): calling benchmark_finibatch()\n");
		r.re_errors = ret = benchmark_finibatch(arg);
		if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): benchmark_finibatch() returned %d\n", ret);
	}

	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): calling benchmark_finiworker()\n");
	ret = benchmark_finiworker(arg);
	if (lm_optG >= 9) fprintf(stderr, "DEBUG9: worker_thread(): benchmark_finiworker() returned %d\n", ret);

	return 0;
}

void
worker_process(void)
{
	int			i, ret;
	void			*tsd;

	for (i = 1; i < lm_optT; i++) {
		tsd = gettsd(pindex, i);
		ret = pthread_create(&tids[i], NULL, worker_thread, tsd);
		if (ret != 0) {
			fprintf(stderr, "worker_process(): pthread_create(%p, NULL, %p, %p) failed: (%d) %s\n", &tids[i], worker_thread, tsd, ret, strerror(ret));
			exit(1);
		}
	}

	tsd = gettsd(pindex, 0);
	(void) worker_thread(tsd);

	for (i = 1; i < lm_optT; i++) {
		ret = pthread_join(tids[i], NULL);
		if (ret != 0) {
			fprintf(stderr, "worker_process(): pthread_join(%p, NULL) failed: (%d) %s\n", tids[i], ret, strerror(ret));
			exit(1);
		}
	}
}

void
usage(void)
{
	(void) printf(
		"usage: %s\n"
		"\t[-1] (single process; overrides -P > 1)\n"
		"\t[-A] (align with clock)\n"
		"\t[-B batch-size (default %d)]\n"
		"\t[-C minimum number of samples (default %d)]\n"
		"\t[-D minimum duration in ms (default %dms)]\n"
		"\t[-E] (echo name to stderr)\n"
		"\t[-G framework debugging level]\n"
		"\t[-H] (suppress headers)\n"
		"\t[-I nsecs per op] (used to compute batch size)\n"
		"\t[-L] (print argument line)\n"
		"\t[-M] (reports mean rather than median)\n"
		"\t[-N test-name (default '%s')]\n"
		"\t[-P processes (default %d)]\n"
		"\t[-S] (print detailed stats)\n"
		"\t[-T threads (default %d)]\n"
		"\t[-V] (print the libMicro version and exit)\n"
		"\t[-W] (flag possible benchmark problems)\n"
		"\t[-X maximum duration in ms (default %dms)]\n"
		"%s\n",
		lm_procname,
		lm_defB, lm_defC, lm_defD, lm_procname, lm_defP, lm_defT, lm_defX,
		lm_usage);
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
		float median = b->ba_corrected.st_median;
		if (median <= 0.0) median = 1.0;
		increase = (long long)(floor((nsecs_resolution * 100.0) /
			((double)lm_optB * median * 1000.0)) +
			1.0);
		(void) printf("#%*sQuantization error likely; "
				"increase batch size (-B option, "
				"currently %d) %lldX to avoid.\n",
				WARNING_INDENT, "", lm_optB, increase);
	}

	long long per_batch = (long long)round((b->ba_count / (double)b->ba_batches));

	if (lm_optG >= 2)
		fprintf(stderr, "DEBUG2: print_warnings(): "
				"lm_optB = %d, per_batch = %lld, "
				"b->ba_count (%lld) / b->ba_batches (%d) = %.2lf\n",
				lm_optB, per_batch, b->ba_count, b->ba_batches,
				b->ba_count / (double)b->ba_batches);

	if ((per_batch / (double)b->ba_batches) < 0.01618) {
		/*
		 * The ratio of 0.01618, or The Golden Ratio / 100, is just a way to
		 * catch a test run with small number of runs in a batch with
		 * potentially large numbers of batches.
		 *
		 * FIXME: There might be a more suitable ratio.
		 */
		if (!head++) {
			(void) printf("#\n# WARNINGS\n");
		}

		/*
		 * The number of batches is either really high, or the number of runs
		 * per batch is really low. In either case, we should probably
		 * re-balance the test run so that a sufficient count of operations is
		 * timed per batch, lowering the number of over samples.
		 */
		increase = (long long)round(((double)b->ba_count / DEF_SAMPLES) / per_batch);
		(void) printf("#%*sLow runs (%lld) per sample (%d samples) "
				"consider increasing batch size (-B option, "
				"currently %d) %lldX (to about %lld) to avoid.\n",
				WARNING_INDENT, "",
				per_batch, b->ba_batches, lm_optB, increase,
				(long long)round((double)b->ba_count / DEF_SAMPLES));
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

	/*
	 * XXX should warn on median != mean by a lot
	 */

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

	if (b->ba_errors) {
		if (!head++) {
			(void) printf("#\n# WARNINGS\n");
		}
		(void) printf("#%*sErrors occured during benchmark.\n", WARNING_INDENT, "");
	}
}

#define STATS_FORMAT	"# %*s %12.5f %*s %12.5f"
#define STATS_FIRST_COLUMN	25
#define STATS_SEP_WIDTH	10

void
print_stats(barrier_t *b)
{
	if (b->ba_count == 0) {
		return;
	}

	(void) printf("#\n");
	(void) printf("# %*s %12s %-*s %12s %s\n",
			STATS_FIRST_COLUMN, "STATISTICS",
			"usecs/call", STATS_SEP_WIDTH, "(raw)",
			"usecs/call", "(outliers removed)");

	(void) printf(STATS_FORMAT "\n", STATS_FIRST_COLUMN, "min",
			b->ba_raw.st_min,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_min);
	(void) printf(STATS_FORMAT "\n", STATS_FIRST_COLUMN, "max",
			b->ba_raw.st_max,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_max);
	(void) printf(STATS_FORMAT "%s\n", STATS_FIRST_COLUMN, "mean",
			b->ba_raw.st_mean,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_mean,
			lm_optM?"*":"");
	(void) printf(STATS_FORMAT "%s\n", STATS_FIRST_COLUMN, "median",
			b->ba_raw.st_median,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_median,
			lm_optM?"":"*");
	(void) printf(STATS_FORMAT "\n", STATS_FIRST_COLUMN, "stddev",
			b->ba_raw.st_stddev,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_stddev);
	(void) printf(STATS_FORMAT "\n", STATS_FIRST_COLUMN, "standard error",
			b->ba_raw.st_stderr,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_stderr);
	(void) printf(STATS_FORMAT "\n", STATS_FIRST_COLUMN, "99% confidence level",
			b->ba_raw.st_99confidence,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_99confidence);
	(void) printf(STATS_FORMAT "\n", STATS_FIRST_COLUMN, "skew",
			b->ba_raw.st_skew,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_skew);
	(void) printf(STATS_FORMAT "\n", STATS_FIRST_COLUMN, "kurtosis",
			b->ba_raw.st_kurtosis,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_kurtosis);
	(void) printf(STATS_FORMAT "\n", STATS_FIRST_COLUMN, "time correlation",
			b->ba_raw.st_timecorr,
			STATS_SEP_WIDTH, "",
			b->ba_corrected.st_timecorr);

	(void) printf("#\n# %*s %12.5f\n#\n",
			STATS_FIRST_COLUMN, "elasped time",
			(b->ba_endtime - b->ba_starttime) / 1.0e9);

	(void) printf("# %*s %12d\n", STATS_FIRST_COLUMN, "number of samples",
			b->ba_batches);
	if (b->ba_batches > b->ba_datasize)
		(void) printf("# %*s %12d (%d samples dropped)\n",
				STATS_FIRST_COLUMN, "number of samples retained",
				b->ba_datasize, (b->ba_batches - b->ba_datasize));
	(void) printf("# %*s %12d\n", STATS_FIRST_COLUMN, "number of outliers",
			b->ba_outliers);
	(void) printf("# %*s %12d\n", STATS_FIRST_COLUMN, "number of final samples",
			b->ba_batches_final);
	(void) printf("# %*s %12d\n", STATS_FIRST_COLUMN, "getnsecs overhead",
			(int)nsecs_overhead);

	print_histo(b);

	if (lm_optW) {
		print_warnings(b);
	}
}

void
update_stats(barrier_t *b, result_t *r)
{
	double	time;
	double	nsecs_per_call;

	if (b->ba_waiters == 0) {
		/* first thread only */
		b->ba_t0 = r->re_t0;
		b->ba_t1 = r->re_t1;
		b->ba_count0 = 0;
		b->ba_errors0 = 0;
	} else {
		/* all but first thread */
		if (r->re_t0 < b->ba_t0) {
			b->ba_t0 = r->re_t0;
		}
		if (r->re_t1 > b->ba_t1) {
			b->ba_t1 = r->re_t1;
		}
	}

	b->ba_count0  += r->re_count;
	b->ba_errors0 += r->re_errors;

	if (b->ba_waiters == b->ba_hwm - 1) {
		/* last thread only */

		time = (double)b->ba_t1 - (double)b->ba_t0 -
			(double)nsecs_overhead;

		if (time < (100 * nsecs_resolution))
			b->ba_quant++;

		/*
		 * normalize by procs * threads if not -U
		 *
		 * FIXME: Should we not be getting the data from each thread in all
		 * the processes and then averaging them all together?
		 */

		nsecs_per_call = (time / (double)b->ba_count0) *
			(double)(lm_optT * lm_optP);

		long long orig_ba_count = b->ba_count;
		b->ba_count	 += b->ba_count0;
		if (lm_optG >= 8) fprintf(stderr, "DEBUG8: update_stats(): b->ba_count (%lld) + b->ba_count0 (%lld) = b->ba_count (%lld)\n", orig_ba_count, b->ba_count0, b->ba_count);
		b->ba_errors += b->ba_errors0;

		b->ba_data[b->ba_batches % b->ba_datasize] =
			nsecs_per_call;

		b->ba_batches++;
	}
}

#if defined(__FreeBSD__) && !defined(USE_SEMOP)
# error "Use of SEMOP barriers required on FreeBSD"
#endif

#ifdef USE_SEMOP
barrier_t *
barrier_create(int hwm, int datasize)
{
	struct sembuf		s[1];
	barrier_t		*b;

	/*LINTED*/
	b = (barrier_t *)mmap(NULL,
		sizeof (barrier_t) + (datasize - 1) * sizeof (double),
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0L);
	if (b == (barrier_t *)MAP_FAILED) {
		return (NULL);
	}
	b->ba_datasize = datasize;

	b->ba_flag	= 0;
	b->ba_hwm	= hwm;
	b->ba_semid = semget(IPC_PRIVATE, 3, 0600);
	if (b->ba_semid == -1) {
		(void) munmap((void *)b, sizeof (barrier_t));
		return (NULL);
	}

	/* [hwm - 1, 0, 0] */
	s[0].sem_num = 0;
	s[0].sem_op	 = hwm - 1;
	s[0].sem_flg = 0;
	if (semop(b->ba_semid, s, 1) == -1) {
		perror("semop(1)");
		(void) semctl(b->ba_semid, 0, IPC_RMID);
		(void) munmap((void *)b, sizeof (barrier_t));
		return (NULL);
	}

	b->ba_waiters = 0;
	b->ba_phase = 0;

	b->ba_count = 0;
	b->ba_errors = 0;

	return (b);
}

int
barrier_destroy(barrier_t *b)
{
	(void) semctl(b->ba_semid, 0, IPC_RMID);
	(void) munmap((void *)b, sizeof (barrier_t));

	return (0);
}

int
barrier_queue(barrier_t *b, result_t *r)
{
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
		return (-1);
	}

	s[0].sem_num = 1;
	s[0].sem_op	 = -(b->ba_hwm - 1);
	s[0].sem_flg = IPC_NOWAIT;
	if (semop(b->ba_semid, s, 1) == -1) {
		if (errno != EAGAIN) {
			perror("semop(3)");
			return (-1);
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
			return (-1);
		}

		s[0].sem_num = 0;
		s[0].sem_op	 = 1;
		s[0].sem_flg = 0;
		s[1].sem_num = 2;
		s[1].sem_op	 = -1;
		s[1].sem_flg = 0;
		if (semop(b->ba_semid, s, 2) == -1) {
			perror("semop(5)");
			return (-1);
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
			return (-1);
		}
	}

	return (0);
}

#else /* USE_SEMOP */

barrier_t *
barrier_create(int hwm, int datasize)
{
	pthread_mutexattr_t	mattr;
	pthread_condattr_t	cattr;
	barrier_t		*b;

	/*LINTED*/
	b = (barrier_t *)mmap(NULL,
		sizeof (barrier_t) + (datasize - 1) * sizeof (double),
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0L);
	if (b == (barrier_t *)MAP_FAILED) {
		return (NULL);
	}
	b->ba_datasize = datasize;

	b->ba_hwm = hwm;
	b->ba_flag	= 0;

	int ret = pthread_mutexattr_init(&mattr);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_mutexattr_init(%p) failed: (%d) %s\n", &mattr, ret, strerror(ret));
		exit(1);
	}
	(void) pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_mutexattr_setpshared(%p, %d) failed: (%d) %s\n", &mattr, PTHREAD_PROCESS_SHARED, ret, strerror(ret));
		exit(1);
	}

	(void) pthread_condattr_init(&cattr);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_condattr_init(%p) failed: (%d) %s\n", &cattr, ret, strerror(ret));
		exit(1);
	}
	(void) pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_condattr_setpshared(%p, %d) failed: (%d) %s\n", &cattr, PTHREAD_PROCESS_SHARED, ret, strerror(ret));
		exit(1);
	}

	(void) pthread_mutex_init(&b->ba_lock, &mattr);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_mutex_init(%p, %p) failed: (%d) %s\n", &b->ba_lock, &mattr, ret, strerror(ret));
		exit(1);
	}
	(void) pthread_cond_init(&b->ba_cv, &cattr);
	if (ret != 0) {
		fprintf(stderr, "barrier_create(): pthread_cond_init(%p, %p) failed: (%d) %s\n", &b->ba_cv, &cattr, ret, strerror(ret));
		exit(1);
	}

	b->ba_waiters = 0;
	b->ba_phase = 0;

	b->ba_count = 0;
	b->ba_errors = 0;

	return b;
}

int
barrier_destroy(barrier_t *b)
{
	int ret = munmap((void *)b, sizeof (barrier_t));
	if (ret != 0) {
		perror("barrier_destroy(): munmap");
		exit(1);
	}

	return 0;
}

int
barrier_queue(barrier_t *b, result_t *r)
{
	int			phase;

	int ret = pthread_mutex_lock(&b->ba_lock);
	if (ret != 0) {
		fprintf(stderr, "barrier_queue(): pthread_mutex_lock(%p) failed: (%d) %s\n", &b->ba_lock, ret, strerror(ret));
		exit(1);
	}

	if (r != NULL) {
		update_stats(b, r);
	}

	phase = b->ba_phase;

	b->ba_waiters++;
	if (b->ba_hwm == b->ba_waiters) {
		b->ba_waiters = 0;
		b->ba_phase++;
		ret = pthread_cond_broadcast(&b->ba_cv);
		if (ret != 0) {
			fprintf(stderr, "barrier_queue(): pthread_cond_broadcast(%p) failed: (%d) %s\n", &b->ba_cv, ret, strerror(ret));
			exit(1);
		}
	}

	while (b->ba_phase == phase) {
		ret = pthread_cond_wait(&b->ba_cv, &b->ba_lock);
		if (ret != 0) {
			fprintf(stderr, "barrier_queue(): pthread_cond_wait(%p, %p) failed: (%d) %s\n", &b->ba_cv, &b->ba_lock, ret, strerror(ret));
			exit(1);
		}
	}

	ret = pthread_mutex_unlock(&b->ba_lock);
	if (ret != 0) {
		fprintf(stderr, "barrier_queue(): pthread_mutex_unlock(%p) failed: (%d) %s\n", &b->ba_lock, ret, strerror(ret));
		exit(1);
	}

	return 0;
}
#endif /* USE_SEMOP */

int
gettindex(void)
{
	int			i;

	if (tids == NULL) {
		return -1;
	}

	for (i = 1; i < lm_optT; i++) {
		if (pthread_self() == tids[i]) {
			return i;
		}
	}

	return 0;
}

int
getpindex(void)
{
	return pindex;
}

void *
gettsd(int p, int t)
{
	if ((p < 0) || (p >= lm_optP) || (t < 0) || (t >= lm_optT))
		return (NULL);

	return ((void *)((unsigned long)tsdseg +
		(((p * lm_optT) + t) * tsdsize)));
}

#ifdef USE_GETHRTIME
long long
getnsecs(void)
{
	return (gethrtime());
}

long long
getusecs(void)
{
	return (gethrtime() / 1000);
}

#elif USE_RDTSC /* USE_GETHRTIME */

__inline__ long long
rdtsc(void)
{
	unsigned long long x;
	__asm__ volatile(".byte 0x0f, 0x31" : "=A" (x));
	return (x);
}

long long
getusecs(void)
{
	return (rdtsc() * 1000000 / lm_hz);
}

long long
getnsecs(void)
{
	return (rdtsc() * 1000000000 / lm_hz);
}

#elif USE_GTOD /* USE_GETHRTIME */

long long
getusecs(void)
{
	struct timeval		tv;

	(void) gettimeofday(&tv, NULL);

	return ((long long)tv.tv_sec * 1000000LL + (long long) tv.tv_usec);
}

long long
getnsecs(void)
{
	struct timeval		tv;

	(void) gettimeofday(&tv, NULL);

	return ((long long)tv.tv_sec * 1000000000LL +
		(long long) tv.tv_usec * 1000LL);
}

#else /* USE_GETHRTIME */

long long
getusecs(void)
{
	struct timespec		ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);

	return ((long long)ts.tv_sec * 1000000LL + (long long) ts.tv_nsec) / 1000LL;
}

long long
getnsecs(void)
{
	struct timespec		ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);

	return ((long long)ts.tv_sec * 1000000000LL + (long long) ts.tv_nsec);
}

#endif /* USE_GETHRTIME */

void
setfdlimit(int limit)
{
	struct rlimit rlimit;

	if (getrlimit(RLIMIT_NOFILE, &rlimit) < 0) {
		perror("getrlimit");
		exit(1);
	}

	if (rlimit.rlim_cur > limit)
		return; /* no worries */

	rlimit.rlim_cur = limit;

	if (rlimit.rlim_max < limit)
		rlimit.rlim_max = limit;

	if (setrlimit(RLIMIT_NOFILE, &rlimit) < 0) {
		perror("setrlimit");
		exit(3);
	}

	return;
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
			return (-1);
		}

		for (i = 0; i < len - 1; i++)
			if (!isdigit(arg[i]))
				return (-1);
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
			return (-1);
		}

		for (i = 0; i < len - 1; i++)
			if (!isdigit(arg[i]))
				return (-1);
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
doublecmp(const void *p1, const void *p2)
{
	double a = *((double *)p1);
	double b = *((double *)p2);

	if (a > b)
		return 1;
	if (a < b)
		return -1;
	return 0;
}

#define HISTO_INDENT	7
#define HISTO_COL1W	12
#define HISTO_COL2W	14
#define HISTO_COL3W	14
#define HISTO_BARW	32U
#define HISTO_PREC	7

static void
print_histo(barrier_t *b)
{
	int			n;
	int			i;
	int			idx;
	int			last;
	long long	maxcount;
	double		sum;
	double		min;
	double		bucket_width;
	long long	count;
	int			i95;	// Index of 95%ile element
	double		v95;	// Value of 95%ile element
	double		r95;	// Range of values in 95%ile
	double		m95;	// Mean of 95%itle values
	histo_t	   *histo;

	(void) printf("#\n");
	(void) printf("# DISTRIBUTION\n");

	/* calculate how much data we've captured */
	n = b->ba_batches > b->ba_datasize ? b->ba_datasize : b->ba_batches;

	/* find the 95th percentile - index, value and range */
	qsort((void *)b->ba_data, n, sizeof (double), doublecmp);

	/* Skip over any infinity or NaN results */
	for (i95 = ((n * 95) / 100); (i95 > 0); i95--) {
		v95 = b->ba_data[i95];
		if (v95 != INFINITY && v95 != NAN)
			break;
	}

	if ((v95 == INFINITY) || (v95 == NAN)) {
		printf("#%*sNo valid data present.\n", HISTO_INDENT, "");
		return;
	}

	min = b->ba_data[0];
	r95 = v95 - min;

	/* find a suitable min and scale */
	bucket_width = r95 / HISTOSIZE;

	/* create and initialise the histogram */
	histo = calloc(sizeof (histo_t), HISTOSIZE);

	/* populate the histogram */
	last = 0;
	sum = 0.0;
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
	m95 = sum / count;

	/* find the largest bucket */
	maxcount = 0;
	for (i = 0; i < HISTOSIZE; i++)
		if (histo[i].count > 0) {
			last = i;
			if (histo[i].count > maxcount)
				maxcount = histo[i].count;
		}

	(void) printf("#%*s%*s %*s %*s %*s\n", HISTO_INDENT, "",
			HISTO_COL1W, "counts", HISTO_COL2W, "usecs/call",
			HISTO_BARW, "", HISTO_COL3W, "means");

	/* print the buckets */
	for (i = 0; i <= last; i++) {
		(void) printf("#%*s%*lld %*.*f |", HISTO_INDENT, "",
				HISTO_COL1W, histo[i].count,
				HISTO_COL2W, HISTO_PREC, min + (bucket_width * (double)i));

		print_bar(histo[i].count, maxcount, HISTO_BARW);

		if (histo[i].count > 0)
			(void) printf("%*.*f\n",
					HISTO_COL3W, HISTO_PREC, histo[i].sum / histo[i].count);
		else
			(void) printf("%*s\n", HISTO_COL3W, "-");
	}

	/* find the mean of values beyond the 95th percentile */
	sum = 0.0;
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
		(void) printf("%*.*f\n", HISTO_COL3W, HISTO_PREC, sum / count);
	else
		(void) printf("%*s\n", HISTO_COL3W, "-");
	(void) printf("#\n");
	(void) printf("#%*s%*s %*.*f\n", HISTO_INDENT, "",
			HISTO_COL1W, "mean of 95%", HISTO_COL2W, HISTO_PREC, m95);
	(void) printf("#%*s%*s %*.*f\n", HISTO_INDENT, "",
			HISTO_COL1W, "95th %ile", HISTO_COL2W, HISTO_PREC, v95);
}

static void
compute_stats(barrier_t *b)
{
	int i, batches;

	batches = (b->ba_batches >= b->ba_datasize)
		? b->ba_datasize
		: b->ba_batches;

	/*
	 * convert to usecs/call
	 */

	for (i = 0; i < batches; i++)
		b->ba_data[i] /= 1000.0;

	/*
	 * do raw stats
	 */

	(void) crunch_stats(b->ba_data, batches, &b->ba_raw);

	/*
	 * recursively apply 3 sigma rule to remove outliers
	 */

	b->ba_corrected = b->ba_raw;
	b->ba_outliers = 0;

	if (batches > 40) { /* remove outliers */
		int removed;

		do {
			removed = remove_outliers(b->ba_data, batches,
				&b->ba_corrected);
			b->ba_outliers += removed;
			batches -= removed;
			(void) crunch_stats(b->ba_data, batches,
				&b->ba_corrected);
			} while (removed != 0 && batches > 40);
	}
	b->ba_batches_final = batches;

	if (b->ba_count == 0) {
		b->ba_errors++;
	}
}

/*
 * routine to compute various statistics on array of doubles.
 */

static int
crunch_stats(double *data, int count, stats_t *stats)
{
	double	a;
	double	std;
	double	diff;
	double	sk;
	double	ku;
	double	mean;
	int		i;
	int		bytes;
	double *dupdata;

	/*
	 * first we need the mean
	 */

	mean = 0.0;

	for (i = 0; i < count; i++) {
		mean += data[i];
	}

	mean /= count;

	stats->st_mean = mean;

	/*
	 * malloc and sort so we can do median
	 */

	dupdata = malloc(bytes = sizeof (double) * count);
	(void) memcpy(dupdata, data, bytes);
	qsort((void *)dupdata, count, sizeof (double), doublecmp);
	stats->st_median = dupdata[count/2];

	/*
	 * reuse dupdata to compute time correlation of data to
	 * detect interesting time-based trends
	 */

	for (i = 0; i < count; i++)
		dupdata[i] = (double)i;

	(void) fit_line(dupdata, data, count, &a, &stats->st_timecorr);
	free(dupdata);

	std = 0.0;
	sk	= 0.0;
	ku	= 0.0;

	stats->st_max = -1;
	stats->st_min = 1.0e99; /* hard to find portable values */

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
	stats->st_99confidence = stats->st_stderr * 2.326;
	double std3		   = (std * std * std);
	stats->st_skew	   = (sk / (cm1 * std3));
	stats->st_kurtosis = (ku / (cm1 * (std3 * std))) - 3;

	return 0;
}

/*
 * does a least squares fit to the set of points x, y and
 * fits a line y = a + bx. Returns a, b
 */

int
fit_line(double *x, double *y, int count, double *a, double *b)
{
	double sumx, sumy, sumxy, sumx2;
	double denom;
	int i;

	sumx = sumy = sumxy = sumx2 = 0.0;

	for (i = 0; i < count; i++) {
		sumx	+= x[i];
		sumx2	+= x[i] * x[i];
		sumy	+= y[i];
		sumxy	+= x[i] * y[i];
	}

	denom = count * sumx2 - sumx * sumx;

	if (denom == 0.0)
		return -1;

	*a = (sumy * sumx2 - sumx * sumxy) / denom;

	*b = (count * sumxy - sumx * sumy) / denom;

	return 0;
}

/*
 * empty function for measurement purposes
 */

int
nop(void)
{
	return 1;
}

#define	NSECITER 1000

static long long
get_nsecs_overhead(void)
{
	long long s;

	double data[NSECITER];
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

	(void) crunch_stats(data, count, &stats);

	while ((outliers = remove_outliers(data, count, &stats)) != 0) {
		count -= outliers;
		(void) crunch_stats(data, count, &stats);
	}

	return (long long)stats.st_mean;

}

/*
 * Determine the resolution of the system's high resolution counter. Most
 * hardware has a nanosecond resolution counter, but some systems still use
 * course resolution (e.g. derived instead by a periodic interrupt).
 *
 * Algorithm:
 * Determine a busy loop that is long enough for successive nanosecond counter
 * reads to report different times. Then take 1000 samples with busy loop
 * interval successively increases by i. The counter resolution is assumed to
 * be the smallest non-zero time delta between these 1000 samples.
 *
 * One last wrinkle is all 1000 samples may have the same delta on a system
 * with a very fast and consistent hardware counter based getnsecs(). In that
 * case assume the resolution is 1ns.
 */
long long
get_nsecs_resolution(void)
{
	long long y[1000];

	volatile int i, j;
	int nops, res;
	long long start, stop;

	/*
	 * first, figure out how many nops to use
	 * to get any delta between time measurements.
	 * use a minimum of one.
	 */

	/*
	 * warm cache
	 */

	stop = start = getnsecs();

	for (i = 1; i < 10000000; i++) {
		start = getnsecs();
		for (j = i; j; j--)
			;
		stop = getnsecs();
		if (stop > start)
			break;
	}

	nops = i;

	/*
	 * now collect data at linearly varying intervals
	 */

	for (i = 0; i < 1000; i++) {
		start = getnsecs();
		for (j = nops * i; j; j--)
			;
		stop = getnsecs();
		y[i] = stop - start;
	}

	/*
	 * find smallest positive difference between samples;
	 * this is the counter resolution
	 */

	res = y[0];
	for (i = 1; i < 1000; i++) {
		int diff = y[i] - y[i-1];

		if (diff > 0 && res > diff)
			res = diff;

	}
	if (res == 0)
		res = 1;

	return res;
}

/*
 * remove any data points from the array more than 3 sigma out
 */

static int
remove_outliers(double *data, int count, stats_t *stats)
{
	double outmin = stats->st_mean - 3 * stats->st_stddev;
	double outmax = stats->st_mean + 3 * stats->st_stddev;

	int i, j, outliers;

	for (outliers = i = j = 0; i < count; i++)
		if (data[i] > outmax || data[i] < outmin)
			outliers++;
		else
			data[j++] = data[i];

	return outliers;
}
