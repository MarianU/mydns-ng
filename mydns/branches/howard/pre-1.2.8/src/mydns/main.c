/**************************************************************************************************
	$Id: main.c,v 1.122 2005/12/08 17:45:56 bboy Exp $

	Copyright (C) 2002-2005  Don Moore <bboy@bboy.net>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at Your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**************************************************************************************************/

#include "named.h"


QUEUE 		*TaskArray[PERIODIC_TASK+1][LOW_PRIORITY_TASK+1];

int		max_used_fd = -1;
struct timeval	current_tick;			/* Current micro-second time */
time_t		current_time;			/* Current time */

static int	servers = 0;			/* Number of server processes to run */
ARRAY		*Servers = NULL;

static int	got_sigusr1 = 0,
	   	got_sigusr2 = 0,
	   	got_sighup = 0,
	   	got_sigalrm = 0,		/* Signal flags */
	   	got_sigchld = 0;		/* Signal flags */
static int 	shutting_down = 0;		/* Shutdown in progress? */

static int	plain_maxfd = 0;

int		run_as_root = 0;		/* Run as root user? */
uint32_t 	answer_then_quit = 0;		/* Answer this many queries then quit */
char		hostname[256];			/* Hostname of local machine */

extern int	*tcp4_fd, *udp4_fd;		/* Listening FD's (IPv4) */
extern int	num_tcp4_fd, num_udp4_fd;	/* Number of listening FD's (IPv4) */
#if HAVE_IPV6
extern int	*tcp6_fd, *udp6_fd;		/* Listening FD's (IPv6) */
extern int	num_tcp6_fd, num_udp6_fd;	/* Number of listening FD's (IPv6) */
#endif

int		show_data_errors = 1;		/* Output data errors? */

SERVERSTATUS	Status;				/* Server status information */

int		Max_FDs = 64;			/* Maximum number of File Descriptors available */

extern void	create_listeners(void);


typedef void (*INITIALTASKSTART)();

typedef struct {
  INITIALTASKSTART start;
  char *subsystem;
} INITIALTASK;

INITIALTASK	master_initial_tasks[] = {
  { ixfr_start,		"IXFR" },
  { NULL,		NULL }
};

INITIALTASK	primary_initial_tasks[] = {
  { notify_start,	"NOTIFY" },
  { task_start,		"TASK" },
  { NULL,		NULL }
};

INITIALTASK	process_initial_tasks[] = {
  { task_start,		"TASK" },
  { NULL,		NULL }
};

static SERVER	*spawn_server(INITIALTASK *);

/**************************************************************************************************
	USAGE
	Display program usage information.
**************************************************************************************************/
static void
usage(int status) {
  if (status != EXIT_SUCCESS) {
    fprintf(stderr, _("Try `%s --help' for more information."), progname);
    fputs("\n", stderr);
  } else {
    printf(_("Usage: %s [OPTION]..."), progname);
    puts("");
    puts(_("Listen for and respond to Internet domain name queries."));
    puts("");
/*  puts("----------------------------------------------------------------------------78");  */
    puts(_("  -b, --background        run as a daemon (move process into background)"));
    puts(_("  -c, --conf=FILE         read config from FILE instead of the default"));
    puts(_("      --create-tables     output table creation SQL and exit"));
    puts(_("      --dump-config       output configuration and exit"));
    printf("                          (%s: \"%s\")\n", _("default"), MYDNS_CONF);
    puts("");
    puts(_("  -D, --database=DB       database name to use"));
    puts(_("  -h, --host=HOST         connect to database at HOST"));
    puts(_("  -p, --password=PASS     password for database (or prompt from tty)"));
    puts(_("  -u, --user=USER         username for database if not current user"));
    puts("");
#if DEBUG_ENABLED
    puts(_("  -d, --debug             enable debug output"));
#endif
    puts(_("  -v, --verbose           be more verbose while running"));
    puts(_("      --no-data-errors    don't output errors about bad data"));
    puts(_("      --help              display this help and exit"));
    puts(_("      --version           output version information and exit"));
    puts("");
    printf(_("The %s homepage is at %s\n"), PACKAGE_NAME, PACKAGE_HOMEPAGE);
    puts("");
    printf(_("Report bugs to <%s>.\n"), PACKAGE_BUGREPORT);
  }
  exit(status);
}
/*--- usage() -----------------------------------------------------------------------------------*/


/**************************************************************************************************
	CMDLINE
	Process command line options.
**************************************************************************************************/
static void
cmdline(int argc, char **argv) {
  char	*optstr;
  int	want_dump_config = 0, optc, optindex;
  int	do_create_tables = 0;
  struct option const longopts[] = {
    {"background",		no_argument,			NULL,	'b'},
    {"conf",			required_argument,		NULL,	'c'},
    {"create-tables",		no_argument,			NULL,	0},
    {"dump-config",		no_argument,			NULL,	0},

    {"database",		required_argument,		NULL,	'D'},
    {"host",			required_argument,		NULL,	'h'},
    {"password",		optional_argument,		NULL,	'p'},
    {"user",			required_argument,		NULL,	'u'},

    {"debug",			no_argument,			NULL,	'd'},
    {"verbose",			no_argument,			NULL,	'v'},
    {"help",			no_argument,			NULL,	0},
    {"version",			no_argument,			NULL,	0},

    /* Undocumented.. Useful when debugging */
    {"quit-after",		required_argument,		NULL,	0},
    {"run-as-root",		no_argument,			NULL,	0},

    {"no-data-errors",	no_argument,				NULL,	0},

    {NULL,			0,				NULL, 0}
  };

  error_init(argv[0], LOG_DAEMON);			/* Init output/logging routines */

  optstr = getoptstr(longopts);

  while ((optc = getopt_long(argc, argv, optstr, longopts, &optindex)) != -1) {
    switch (optc) {
    case 0:
      {
	const char *opt = longopts[optindex].name;

	if (!strcmp(opt, "version")) {			/* --version */
	  printf("%s ("PACKAGE_NAME") "PACKAGE_VERSION" ("SQL_VERSION_STR")\n", progname);
	  puts("\n" PACKAGE_COPYRIGHT);
	  puts(_("This is free software; see the source for copying conditions.  There is NO"));
	  puts(_("warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."));
	  exit(EXIT_SUCCESS);
	} else if (!strcmp(opt, "help"))		/* --help */
	  usage(EXIT_SUCCESS);
	else if (!strcmp(opt, "dump-config"))		/* --dump-config */
	  want_dump_config++;
	else if (!strcmp(opt, "create-tables"))		/* --create-tables */
	  do_create_tables = 1;
	else if (!strcmp(opt, "quit-after"))		/* --quit-after */
	  answer_then_quit = strtoul(optarg, (char **)NULL, 10);
	else if (!strcmp(opt, "run-as-root"))		/* --run-as-root */
	  run_as_root = 1;
	else if (!strcmp(opt, "no-data-errors"))	/* --no-data-errors */
	  show_data_errors = 0;
      }
      break;

    case 'b':						/* -b, --background */
      opt_daemon = 1;
      break;

    case 'c':						/* -c, --conf=FILE */
      opt_conf = optarg;
      break;

    case 'd':						/* -d, --debug */
#if DEBUG_ENABLED
      err_verbose = err_debug = 1;
#endif
      break;

    case 'D':						/* -D, --database=DB */
      conf_set(&Conf, "database", optarg, 0);
      break;

    case 'h':						/* -h, --host=HOST */
      conf_set(&Conf, "db-host", optarg, 0);
      break;

    case 'p':						/* -p, --password=PASS */
      if (optarg) {
	conf_set(&Conf, "db-password", optarg, 0);
	memset(optarg, 'X', strlen(optarg));
      } else
	conf_set(&Conf, "db-password", passinput(_("Enter password")), 0);
      break;

    case 'u':						/* -u, --user=USER */
      conf_set(&Conf, "db-user", optarg, 0);
      break;

    case 'v':						/* -v, --verbose */
      err_verbose = 1;
      break;

    default:
      usage(EXIT_FAILURE);
    }
  }

  if (optind < argc)
    fprintf(stderr, "%s: %s\n", progname, _("Extraneous command-line arguments ignored"));

  load_config();

  if (do_create_tables) {
    db_output_create_tables();
    exit (EXIT_SUCCESS);
  }

  if (want_dump_config) {
    dump_config();
    exit(EXIT_SUCCESS);
  }

  db_verify_tables();					/* Make sure tables are OK */

  /* Random numbers are just for round robin and load balancing */
  srand(time(NULL));
}
/*--- cmdline() ---------------------------------------------------------------------------------*/


#define set_sighandler(SIG,H) __set_sighandler((SIG),(H),NULL)

/**************************************************************************************************
	SET_SIGHANDLER
**************************************************************************************************/
typedef void (*sig_handler)(int);

static void
__set_sighandler(int sig, sig_handler h, sigset_t *mask) {
  struct sigaction act;

  if (mask == NULL) {
    sigemptyset(&act.sa_mask);
  } else {
    memcpy(&act.sa_mask, mask, sizeof(sigset_t));
  }
  act.sa_flags = 0;

  if (sig == SIGALRM) {
#ifdef SA_INTERRUPT
    act.sa_flags |= SA_INTERRUPT;
#endif
  } else {
#ifdef SA_NOCLDSTOP
    act.sa_flags |= SA_NOCLDSTOP;
#endif
#ifdef SA_RESTART
    act.sa_flags |= SA_RESTART;
#endif
  }

  act.sa_handler = h;
  sigaction(sig, &act, NULL);
}
/*--- set_sighandler() --------------------------------------------------------------------------*/


/**************************************************************************************************
	BECOME_DAEMON
**************************************************************************************************/
static void
become_daemon(void) {
  int pid;

  sql_close(sql);

  if ((pid = fork()) < 0)
    Err(_("fork"));
  if (pid)
    _exit(EXIT_SUCCESS);

  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  setsid();

  db_connect();
}
/*--- become_daemon() ---------------------------------------------------------------------------*/


/**************************************************************************************************
	CREATE_PIDFILE
	Creates the PID file.
**************************************************************************************************/
static void
create_pidfile(void) {
  char *name = conf_get(&Conf, "pidfile", NULL);
  FILE *fp;

  if (!(fp = fopen(name, "w")))
    Err("%s", name);
  fprintf(fp, "%lu\n", (unsigned long)getpid());
  fclose(fp);

  /* Change ownership so we can delete it later */
  chown(name, perms_uid, perms_gid);
}
/*--- create_pidfile() --------------------------------------------------------------------------*/


/**************************************************************************************************
	SERVER_STATUS
**************************************************************************************************/
void
server_status(void) {
  char			buf[1024], *b = buf;
  time_t		uptime = time(NULL) - Status.start_time;
  unsigned long 	requests = Status.udp_requests + Status.tcp_requests;

  b += snprintf(b, sizeof(buf)-(b-buf), "%s ", hostname);
  b += snprintf(b, sizeof(buf)-(b-buf), "%s %s (%lus) ", _("up"), strsecs(uptime),
		(unsigned long)uptime);
  b += snprintf(b, sizeof(buf)-(b-buf), "%lu %s ", requests, _("questions"));
  b += snprintf(b, sizeof(buf)-(b-buf), "(%.0f/s) ", requests ? AVG(requests, uptime) : 0.0);
  b += snprintf(b, sizeof(buf)-(b-buf), "NOERROR=%u ", Status.results[DNS_RCODE_NOERROR]);
  b += snprintf(b, sizeof(buf)-(b-buf), "SERVFAIL=%u ", Status.results[DNS_RCODE_SERVFAIL]);
  b += snprintf(b, sizeof(buf)-(b-buf), "NXDOMAIN=%u ", Status.results[DNS_RCODE_NXDOMAIN]);
  b += snprintf(b, sizeof(buf)-(b-buf), "NOTIMP=%u ", Status.results[DNS_RCODE_NOTIMP]);
  b += snprintf(b, sizeof(buf)-(b-buf), "REFUSED=%u ", Status.results[DNS_RCODE_REFUSED]);

  /* If the server is getting TCP queries, report on the percentage of TCP queries */
  if (Status.tcp_requests)
    b += snprintf(b, sizeof(buf)-(b-buf), "(%d%% TCP, %lu queries)",
		  (int)PCT(requests, Status.tcp_requests),
		  (unsigned long)Status.tcp_requests);

  Notice("%s", buf);
}
/*--- server_status() ---------------------------------------------------------------------------*/

void
kill_server(SERVER *server, int sig) {
  kill(server->pid, sig);
}

/**************************************************************************************************
	SIGUSR1
	Outputs server stats.
**************************************************************************************************/
void
master_sigusr1(int dummy) {
  int n;
  for (n = 0; n < array_numobjects(Servers); n++)
    kill_server((SERVER*)array_fetch(Servers, n), SIGUSR1);
  got_sigusr1 = 0;
}

void
sigusr1(int dummy) {
  server_status();
  got_sigusr1 = 0;
}
/*--- sigusr1() ---------------------------------------------------------------------------------*/


/**************************************************************************************************
	SIGUSR2
	Outputs cache stats.
**************************************************************************************************/
void
master_sigusr2(int dummy) {
  int n;
  for (n = 0; n < array_numobjects(Servers); n++)
    kill_server((SERVER*)array_fetch(Servers, n), SIGUSR2);
  got_sigusr2 = 0;
}

void
sigusr2(int dummy) {
  cache_status(ZoneCache);
#if USE_NEGATIVE_CACHE
  cache_status(NegativeCache);
#endif
  cache_status(ReplyCache);
  got_sigusr2 = 0;
}
/*--- sigusr2() ---------------------------------------------------------------------------------*/


/**************************************************************************************************
	SIGHUP
**************************************************************************************************/
void
master_sighup(int dummy) {
  int n;
  for (n = 0; n < array_numobjects(Servers); n++)
    kill_server((SERVER*)array_fetch(Servers, n), SIGHUP);
  got_sighup = 0;
}

void
sighup(int dummy)
{
  cache_empty(ZoneCache);
#if USE_NEGATIVE_CACHE
  cache_empty(NegativeCache);
#endif
  cache_empty(ReplyCache);
  db_check_optional();
  Notice(_("SIGHUP received: cache emptied, tables reloaded"));
  got_sighup = 0;
}
/*--- sighup() ----------------------------------------------------------------------------------*/


/**************************************************************************************************
	SIGNAL_HANDLER
**************************************************************************************************/
void
signal_handler(int signo) {
  switch (signo) {
  case SIGHUP:
    got_sighup = 1; break;
  case SIGUSR1:
    got_sigusr1 = 1; break;
  case SIGUSR2:
    got_sigusr2 = 1; break;
  case SIGALRM:
    got_sigalrm = 1; break;
  case SIGCHLD:
    got_sigchld = 1; break;
  default:
    break;
  }
}
/*--- signal_handler() --------------------------------------------------------------------------*/


static void
close_task_queue(QUEUE *TaskP) {

  register TASK *t;

  /* Close any TCP connections and any NOTIFY sockets */
  for (t = TaskP->head; t; t = TaskP->head) {
    if (t->protocol == SOCK_STREAM && t->fd != -1)
      sockclose(t->fd);
    else if (t->protocol == SOCK_DGRAM
	     && (t->status & (ReqTask|TickTask|RunTask))
	     && t->fd != -1)
      sockclose(t->fd);
    dequeue(t);
  }
}

static void
free_all_tasks() {
  int i, j;

  for (i = NORMAL_TASK; i <= PERIODIC_TASK; i++) {
    for (j = HIGH_PRIORITY_TASK; j <= LOW_PRIORITY_TASK; j++) {
      QUEUE *TaskQ = TaskArray[i][j];
      close_task_queue(TaskQ);
      /*TaskArray[i][j] = NULL;*/
    }
  }
}

void
free_other_tasks(TASK *t, int closeallfds) {
  int i, j;

  for (i = NORMAL_TASK; i <+ PERIODIC_TASK; i++) {
    for (j = HIGH_PRIORITY_TASK; j <= LOW_PRIORITY_TASK; j++) {
      QUEUE *TaskQ = TaskArray[i][j];
      TASK *curtask = TaskQ->head;
      TASK *nexttask;
      while (curtask) {
	nexttask = curtask->next;
	if (curtask == t) continue;
	/* Do not sockclose as the other end(s) are still inuse */
	if (closeallfds && (t->protocol != SOCK_STREAM) && t->fd >= 0) close(t->fd);
	dequeue(curtask);
	curtask = nexttask;
      }
    }
  }
}

/**************************************************************************************************
	NAMED_CLEANUP
**************************************************************************************************/
void
named_cleanup(int signo) {

  shutting_down = signo;

}

void
named_shutdown(int signo) {
  int n;

  switch (signo) {
  case 0:
    Notice(_("Normal shutdown")); break;
  case SIGINT:
    Notice(_("interrupted")); break;
  case SIGQUIT:
    Notice(_("quit")); break;
  case SIGTERM:
    Notice(_("terminated")); break;
  case SIGABRT:
    Notice(_("aborted")); break;
  default:
    Notice(_("exiting due to signal %d"), signo); break;
  }

  server_status();

  free_all_tasks();

  cache_empty(ZoneCache);
#if USE_NEGATIVE_CACHE
  cache_empty(NegativeCache);
#endif
  cache_empty(ReplyCache);

  /* Close listening FDs - do not sockclose these are shard with other processes */
  for (n = 0; n < num_tcp4_fd; n++)
    close(tcp4_fd[n]);

  for (n = 0; n < num_udp4_fd; n++)
    close(udp4_fd[n]);

#if HAVE_IPV6
  for (n = 0; n < num_tcp6_fd; n++)
    close(tcp6_fd[n]);

  for (n = 0; n < num_udp6_fd; n++)
    close(udp6_fd[n]);
#endif	/* HAVE_IPV6 */

}

void
master_shutdown(int signo) {
  int i, m, n, status, running_procs = 0;

  named_shutdown(signo);

  for (n = 0; n < array_numobjects(Servers); n++) {
    SERVER	*server = (SERVER*)array_fetch(Servers, n);
    if(server->pid != -1) {
      running_procs += 1;
      kill_server(server, signo);
    }
  }

  i = 0;
  while (running_procs) {
    n = waitpid(-1, &status, WNOHANG);
    if (n < 0) {
      if (errno == ECHILD) break;
      Notice(_("waitpid returned error %s(%d)"), strerror(errno), errno);
    }
    if (n <= 0) {
      i += 1;
      if (i == 10) break;
      sleep(1);
      continue;
    }
    running_procs -= 1;
    for (m = 0; m < array_numobjects(Servers); m++) {
      SERVER	*server = (SERVER*)array_fetch(Servers, m);
      if (server->pid == n) {
	server->pid = -1;
	break;
      }
    }
  }
  if (running_procs) {
    Notice(_("%d Clones did not die"), running_procs);
  }
  for (n = 0; n < array_numobjects(Servers); n++) {
      SERVER	*server = (SERVER*)array_fetch(Servers, n);
    if (server->pid != -1) {
      Notice(_("Did not see clone %d - pid %d die"), n, server->pid);
    }
  }
   
  /* Kill our in-direct children */
  killpg(0, signo);

  /* Close listening FDs - sockclose these as we want everything to go away */
  for (n = 0; n < num_tcp4_fd; n++)
    sockclose(tcp4_fd[n]);

  for (n = 0; n < num_udp4_fd; n++)
    sockclose(udp4_fd[n]);

#if HAVE_IPV6
  for (n = 0; n < num_tcp6_fd; n++)
    sockclose(tcp6_fd[n]);

  for (n = 0; n < num_udp6_fd; n++)
    sockclose(udp6_fd[n]);
#endif	/* HAVE_IPV6 */

  unlink(conf_get(&Conf, "pidfile", NULL));
}

/*--- named_cleanup() ---------------------------------------------------------------------------*/


/**************************************************************************************************
	CHILD_CLEANUP
**************************************************************************************************/
static int
reap_child() {
  int status;
  int pid;

  if ((pid = waitpid(-1, &status, WNOHANG)) < 0) return -1;

  if (pid == 0) return 0;

  if (WIFEXITED(status)) {
    ;
  } else {
#ifdef WCOREDUMP
    if (WIFSIGNALED(status))
      Warnx(_("pid %d exited due to signal %d%s"), pid, WTERMSIG(status),
	    WCOREDUMP(status) ? _(" (core dumped)") : "");
    else
      Warnx(_("pid %d exited with status %d%s"), pid, WEXITSTATUS(status),
	    WCOREDUMP(status) ? _(" (core dumped)") : "");
#else
    if (WIFSIGNALED(status))
      Warnx(_("pid %d exited due to signal %d"), pid, WTERMSIG(status));
    else
      Warnx(_("pid %d exited with status %d"), pid, WEXITSTATUS(status));
#endif
  }

  return pid;
}

SERVER *
find_server_for_task(TASK *t) {
  int n;

  for (n = 0; n < array_numobjects(Servers); n++) {
    SERVER *server = (SERVER*)array_fetch(Servers, n);
    if (server->listener == t) return server;
  }

  return NULL;
}

static void
master_child_cleanup(int signo) {
  int n, pid;

  got_sigchld = 0;

  while ((pid = reap_child()) > 0) {
    /* If the dead child is part of the child servers restart it */
    for (n = 0; n < array_numobjects(Servers); n++) {
      SERVER	*server = (SERVER*)array_fetch(Servers, n);
      if (pid == server->pid) {
	Notice(_("Server pid %d died"), pid);
	if (shutting_down) server->pid = -1;
	else {
	  if (server->listener) dequeue(server->listener);
	  sockclose(server->serverfd);
	  RELEASE(server);
	  array_store(Servers, n, NULL);
	  if (n == 0) server = spawn_server(primary_initial_tasks);
	  else server = spawn_server(process_initial_tasks);
	  server->listener = mcomms_start(server->serverfd);
	  array_store(Servers, n, server);
	}
	break;
      }
    }
  }
}

static void
child_cleanup(int signo) {
  int pid;

  got_sigchld = 0;

  while ((pid = reap_child()) > 0) {
    ;
  }
}
/*--- child_cleanup() ---------------------------------------------------------------------------*/


/**************************************************************************************************
	_INIT_RLIMIT
	Sets a single resource limit and optionally prints out a notification message; called by
	init_rlimits().
**************************************************************************************************/
static int
_init_rlimit(int resource, const char *desc, long long set) {
  struct rlimit rl;

  if (getrlimit(resource, &rl) < 0)
    Err(_("getrlimit"));
  if (set == -1)
    rl.rlim_cur = rl.rlim_max;
  else if (set > 0 && rl.rlim_cur < set)
    rl.rlim_cur = set;
  setrlimit(resource, &rl);
  if (getrlimit(resource, &rl) < 0)
    Err(_("getrlimit"));
  return rl.rlim_cur;
}
/*--- _init_rlimit() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	INIT_RLIMITS
	Max out allowed resource limits.
**************************************************************************************************/
static void
init_rlimits(void)
{
#ifdef RLIMIT_CPU
	_init_rlimit(RLIMIT_CPU, "RLIMIT_CPU", 0);
#endif
#ifdef RLIMIT_FSIZE
	_init_rlimit(RLIMIT_FSIZE, "RLIMIT_FSIZE", 0);
#endif
#ifdef RLIMIT_DATA
	_init_rlimit(RLIMIT_DATA, "RLIMIT_DATA", 0);
#endif
#ifdef RLIMIT_STACK
	_init_rlimit(RLIMIT_STACK, "RLIMIT_STACK", -1);
#endif
#ifdef RLIMIT_CORE
	_init_rlimit(RLIMIT_CORE, "RLIMIT_CORE", 0);
#endif
#ifdef RLIMIT_RSS
	_init_rlimit(RLIMIT_RSS, "RLIMIT_RSS", 0);
#endif
#ifdef RLIMIT_NPROC
	_init_rlimit(RLIMIT_NPROC, "RLIMIT_NPROC", -1);
#endif
#ifdef RLIMIT_NOFILE
	Max_FDs = _init_rlimit(RLIMIT_NOFILE, "RLIMIT_NOFILE", -1);
#endif
#ifdef RLIMIT_MEMLOCK
	_init_rlimit(RLIMIT_MEMLOCK, "RLIMIT_MEMLOCK", 0);
#endif
#ifdef RLIMIT_AS
	_init_rlimit(RLIMIT_AS, "RLIMIT_AS", 0);
#endif
}
/*--- init_rlimits() ----------------------------------------------------------------------------*/

struct timeval *
gettick() {

  gettimeofday(&current_tick, NULL);
  current_time = current_tick.tv_sec;

  return (&current_tick);
}

static void
do_initial_tasks(INITIALTASK *initial_tasks) {
  int i;

  
  for (i = 0; initial_tasks[i].start; i++ ) {
    initial_tasks[i].start();
  }

}

static int
build_fdset_for_tasks(QUEUE *TaskQ, fd_set *rfd, fd_set *wfd, fd_set *efd, int *want_timeout) {
  int maxfd = 0;
  TASK *t, *Next_task;

  for (t = TaskQ->head; t; t = Next_task) {
    int		fd;
    time_t	timedout;

    Next_task = t->next;

    /* Check if task has timed out */
    timedout = t->timeout - current_time;
    if (timedout <= 0) {
      taskexec_t res = task_timedout(t);
      if(!((res == TASK_CONTINUE) || (res == TASK_EXECUTED) || (res == TASK_DID_NOT_EXECUTE)))
	continue;
      timedout = t->timeout - current_time;
      if (timedout < 0) timedout = 0;
    }

    if ((t->status & Needs2Exec) && (t->type != PERIODIC_TASK))
      *want_timeout = 0;
    else
    /* Set timeout to minimum */
      if (timedout < *want_timeout)
	*want_timeout = timedout;

    fd = t->fd;
    if (fd < 0) continue;

    if ((t->status & Needs2Read)) {
      FD_SET(fd, rfd);
      maxfd = (maxfd > fd)?maxfd:fd;
    }
    if ((t->status & Needs2Write)) {
      FD_SET(fd, wfd);
      maxfd = (maxfd > fd)?maxfd:fd;
    }
    if (t->protocol == SOCK_STREAM) {
      FD_SET(fd, efd);
    }
  }

  if (*want_timeout < 0) *want_timeout = 0;
  return maxfd;
}

static int
build_fdsets(int maxfd, fd_set *rfd, fd_set *wfd, fd_set *efd, int *want_timeout) {
  int i, j;

  for (i = NORMAL_TASK; i <= PERIODIC_TASK; i++) {
    for (j = HIGH_PRIORITY_TASK; j <= LOW_PRIORITY_TASK; j++) {
      int fd = build_fdset_for_tasks(TaskArray[i][j], rfd, wfd, efd, want_timeout);
      if (fd > maxfd ) maxfd = fd;
    }
  }
  return maxfd;
}

static int
run_tasks(fd_set *rfd, fd_set*wfd, fd_set *efd) {
  int i,j, tasks_executed = 0;
  TASK *t, *next_task;

  /* Process tasks */
  for (j = HIGH_PRIORITY_TASK; j <= LOW_PRIORITY_TASK; j++) {
    for (i = NORMAL_TASK; i <= PERIODIC_TASK; i++) {
      QUEUE *TaskQ = TaskArray[i][j];
      for (t = TaskQ->head; t; t = next_task) {
	next_task = t->next;
	if (i == IO_TASK) {
	  int fd = t->fd;
	  if (fd >= 0) {
	    if ((t->status & Needs2Read)) {
	      if(!FD_ISSET(fd, rfd) && !FD_ISSET(fd, efd)) continue;
	    } else if ((t->status & Needs2Write)) {
	      if (!FD_ISSET(fd, wfd) && !FD_ISSET(fd, efd)) continue;
	    }
	  }
	}
	tasks_executed += task_process(t, rfd, wfd, efd);
	if (shutting_down) break;
      }
      if (shutting_down) break;
    }
    if (shutting_down) break;
  }
  queue_stats();
  return tasks_executed;
}

static void
server_loop(int plain_maxfd, INITIALTASK *initial_tasks, int serverfd) {

  do_initial_tasks(initial_tasks);

  udp_start();
  tcp_start();

  if (serverfd >= 0) {
    fcntl(serverfd, F_SETFL, fcntl(serverfd, F_GETFL, 0) | O_NONBLOCK);
    scomms_start(serverfd);
  }

  /* Main loop: Read connections and process queue */
  for (;;) {
    int			maxfd;
    int			rv;
    int			want_timeout = task_timeout;
    fd_set		rfd, wfd, efd;
    struct timeval	tv;

    /* Handle signals */
    if (got_sighup) sighup(SIGHUP);
    if (got_sigusr1) sigusr1(SIGUSR1);
    if (got_sigusr2) sigusr2(SIGUSR2);
    if (got_sigchld) child_cleanup(SIGCHLD);

    if(shutting_down) break;

    maxfd = plain_maxfd;
    FD_ZERO(&rfd);
    FD_ZERO(&wfd);
    FD_ZERO(&efd);

    gettick();

    maxfd = build_fdsets(maxfd, &rfd, &wfd, &efd, &want_timeout);
		  
    if (want_timeout  && TaskArray[NORMAL_TASK][HIGH_PRIORITY_TASK]->head) {
      /* If we have a high priority normal task to run then wake up in 1/10th second */
      tv.tv_sec = 0;
      tv.tv_usec = 100000;
    } else {
      /* Otherwise wakeup when IO conditions change or when next task times out */
      tv.tv_sec = want_timeout;
      tv.tv_usec = 0;
    }
    rv = select(maxfd+1, &rfd, &wfd, &efd, &tv);

    if (rv < 0) {
      if (errno == EINTR) continue;
      Err(_("select"));
    }

    gettick();

    if (rv == 0) {
      FD_ZERO(&rfd);
      FD_ZERO(&wfd);
      FD_ZERO(&efd);
    }

    if (shutting_down) break;

    run_tasks(&rfd, &wfd, &efd);

  }

  named_shutdown(shutting_down);

}

static SERVER *
spawn_server(INITIALTASK *initial_tasks) {
  pid_t	pid;
  int	fd[2] = { -1, -1 };
  int	masterfd, serverfd;
  int	res, n;


  res = socketpair(PF_UNIX, SOCK_DGRAM, 0, fd);

  if (res < 0)
    Err(_("socketpair"));

  for (n = 1; n < 1024; n++) {
    int opt = n *1024;
    if ((setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt)) < 0)
      || (setsockopt(fd[1], SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt)) < 0))
      break;
    /* These do not have any effect on UNIX sockets but here for completeness */
    setsockopt(fd[0], SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
    setsockopt(fd[1], SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
  }

  masterfd = fd[0];
  serverfd = fd[1];
  fcntl(masterfd, F_SETFL, fcntl(masterfd, F_GETFL, 0) | O_NONBLOCK);
  fcntl(serverfd, F_SETFL, fcntl(serverfd, F_GETFL, 0) | O_NONBLOCK);

  if ((pid = fork()) < 0)
    Err(_("fork"));

  if (pid > 0) { /* Parent === MASTER */
    SERVER *server = (SERVER*)ALLOCATE(sizeof(SERVER), SERVER);
    server->pid = pid;
    server->serverfd = masterfd;
    server->listener = NULL;
    server->signalled = 0;
    close(serverfd);

    return server;
  }

  /* Child == SERVER */

  /* Delete other server objects as they belong to master.*/
  while (array_numobjects(Servers)) {
    SERVER *server = array_remove(Servers);
    if (server) {
#if DEBUG_ENABLED
      Debug(_("closing fd %d"), server->serverfd);
#endif
      close(server->serverfd);
      RELEASE(server);
    }
  }

  array_free(Servers, 0);
  Servers = NULL;

  close(masterfd);

  /* Delete pre-existing tasks as they belong to master */
  free_all_tasks();
#if DEBUG_ENABLED
  Debug(_("Cleaned up master structures - starting work"));
#endif

  db_connect();

  server_loop(plain_maxfd, initial_tasks, serverfd);

  exit(EXIT_SUCCESS);

  /*NOTREACHED*/
}

static void
master_loop(INITIALTASK *initial_tasks) {
  int	i;

  do_initial_tasks(initial_tasks);

  for (i = 0; i < array_numobjects(Servers); i++) {
    SERVER *server = array_fetch(Servers, i);
    server->listener = mcomms_start(server->serverfd);
  }

  /* Sleep waiting for children and tick tasks to run */
  for (;;) {
    int			rv = 0;
    fd_set		rfd, wfd, efd;
    int			maxfd = 0;
    int			want_timeout = task_timeout;
    struct timeval	tv;

    if (got_sighup) master_sighup(SIGHUP);
    if (got_sigusr1) master_sigusr1(SIGUSR1);
    if (got_sigusr2) master_sigusr2(SIGUSR2);
    if (got_sigchld) master_child_cleanup(SIGCHLD);

    if(shutting_down) break;

    FD_ZERO(&rfd);
    FD_ZERO(&wfd);
    FD_ZERO(&efd);

    gettick();

    maxfd = build_fdsets(maxfd, &rfd, &wfd, &efd, &want_timeout);
		  
    if (TaskArray[NORMAL_TASK][HIGH_PRIORITY_TASK]->head) {
      /* If we have a high priority normal task to run then wake up in 1/10th second */
      tv.tv_sec = 0;
      tv.tv_usec = 100000;
    } else {
      /* Otherwise wakeup when IO conditions change or when next task times out */
      tv.tv_sec = want_timeout;
      tv.tv_usec = 0;
    }
    rv = select(maxfd+1, &rfd, &wfd, &efd, &tv);

    if (rv < 0) {
      if (errno == EINTR) continue;
      Err(_("select"));
    }

    gettick();

    run_tasks(&rfd, &wfd, &efd);

    if (shutting_down) break;

  }

  master_shutdown(shutting_down);

}

/**************************************************************************************************
	MAIN
**************************************************************************************************/
int
main(int argc, char **argv)
{
  int i, j, n;
  sigset_t mask;

  setlocale(LC_ALL, "");				/* Internationalization */
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  cmdline(argc, argv);					/* Process command line */

  /* Set hostname */
  gethostname(hostname, sizeof(hostname)-1);

  sigemptyset(&mask);

  set_sighandler(SIGHUP,  signal_handler);
  set_sighandler(SIGUSR1, signal_handler);
  set_sighandler(SIGUSR2, signal_handler);
  set_sighandler(SIGALRM, signal_handler);
  set_sighandler(SIGCHLD, signal_handler);

  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGQUIT);
  sigaddset(&mask, SIGABRT);
  sigaddset(&mask, SIGTERM);

  __set_sighandler(SIGINT,  named_cleanup, &mask);
  __set_sighandler(SIGQUIT, named_cleanup, &mask);
  __set_sighandler(SIGABRT, named_cleanup, &mask);
  __set_sighandler(SIGTERM, named_cleanup, &mask);

  init_rlimits();

  if (opt_daemon)					/* Move into background if requested */
    become_daemon();

  conf_set_logging();
  db_connect();
  create_pidfile();					/* Create PID file */

  for (i = NORMAL_TASK; i <= PERIODIC_TASK; i++) {
    for (j = HIGH_PRIORITY_TASK; j <= LOW_PRIORITY_TASK; j ++) {
      TaskArray[i][j] = queue_init(task_type_name(i), task_priority_name(j));
    }
  }

  cache_init();						/* Initialize cache */

  /* Start listening fd's */
  create_listeners();

  time(&Status.start_time);

  if (run_as_root) {
    chdir("/tmp");
    Notice("%s", _("WARNING: running with superuser permissions (cwd=/tmp)"));
  } else {
#if PROFILING
    /*
     * If profiling, change to a dir that a user
     * without perms can likely write profiling data to
     */
    chdir("/tmp");
#endif

    /* Drop permissions */
    if (getgid() == 0 && setgid(perms_gid))
      Err(_("error setting group ID to %u"), (unsigned int)perms_gid);
    if (getuid() == 0 && setuid(perms_uid))
      Err(_("error setting user ID to %u"), (unsigned int)perms_uid);
    if (!getgid() || !getuid())
      Errx(_("refusing to run as superuser"));
    check_config_file_perms();
  }


  for (n = 0; n < num_udp4_fd; n++) {
    if (udp4_fd[n] > plain_maxfd)
      plain_maxfd = udp4_fd[n];
  }
  for (n = 0; n < num_tcp4_fd; n++) {
    if (tcp4_fd[n] > plain_maxfd)
      plain_maxfd = tcp4_fd[n];
  }
#if HAVE_IPV6
  for (n = 0; n < num_udp6_fd; n++) {
    if (udp6_fd[n] > plain_maxfd)
      plain_maxfd = udp6_fd[n];
  }
  for (n = 0; n < num_tcp6_fd; n++) {
    if (tcp6_fd[n] > plain_maxfd)
      plain_maxfd = tcp6_fd[n];
  }
#endif

  gettick();

  /* Spawn a process for each server, use multicpu if servers == 1 and multicpu is not -1 */
  servers = atoi(conf_get(&Conf, "servers", NULL));
  if (servers <= 1) {
    int multicpu = atoi(conf_get(&Conf, "multicpu", NULL));
    if (multicpu > 1) servers = multicpu;
  }
    
  if (servers < 0) servers = 1;

  Servers = array_init(servers);

  if (servers) {
    array_append(Servers, (void*)spawn_server(primary_initial_tasks));

    for (n = 1; n < servers; n++) {
      array_append(Servers, (void*)spawn_server(process_initial_tasks));
    }
  
    master_loop(master_initial_tasks);
  } else {
    do_initial_tasks(master_initial_tasks);
    server_loop(plain_maxfd, primary_initial_tasks, -1);
  }

  exit(EXIT_SUCCESS);

  /*NOTREACHED*/
}
/*--- main() ------------------------------------------------------------------------------------*/

/* vi:set ts=3: */
/* NEED_PO */
