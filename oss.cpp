// Author: John Garrett
// Date: 2026-02-10
// Description: Launches and coordinates child processes with configurable limits (stub).

#include <getopt.h>
#include <iomanip>
#include <csignal>
#include <ctime>
#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

struct PCB
{
  int occupied;
  pid_t pid;
  int startSeconds;
  int startNano;
  int endingTimeSeconds;
  int endingTimeNano;
};

static const int kClockInts = 2;
static const int kProcessTableSize = 20;

static volatile sig_atomic_t terminate_requested = 0;
static int g_shm_id = -1;
static int *g_clock = nullptr;
static PCB *g_process_table = nullptr;

static void print_usage(const char *prog)
{
  // Print valid CLI usage for this program.
  cout << "Usage: " << prog << " [-h] [-n proc] [-s simul] [-t timelimit] [-i interval]" << endl;
}

static void print_process_table(const PCB table[], int sec, int nano)
{
  cout << "OSS PID:" << static_cast<int>(getpid())
       << " SysClockS: " << sec << " SysclockNano: " << nano << endl;
  cout << "Process Table:" << endl;
  cout << "Entry Occupied PID  StartS StartN EndingTimeS EndingTimeNano" << endl;
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    cout << setw(5) << i << " " << setw(8) << table[i].occupied << " " << setw(5)
         << static_cast<int>(table[i].pid) << " " << setw(6) << table[i].startSeconds
         << " " << setw(6) << table[i].startNano << " " << setw(11)
         << table[i].endingTimeSeconds << " " << setw(14) << table[i].endingTimeNano
         << endl;
  }
}

static void cleanup_shared_memory()
{
  if (g_clock != nullptr)
  {
    shmdt(g_clock);
    g_clock = nullptr;
  }
  if (g_shm_id != -1)
  {
    shmctl(g_shm_id, IPC_RMID, nullptr);
    g_shm_id = -1;
  }
}

static void kill_all_children()
{
  if (g_process_table == nullptr)
  {
    return;
  }
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    if (g_process_table[i].occupied && g_process_table[i].pid > 0)
    {
      kill(g_process_table[i].pid, SIGTERM);
    }
  }
}

static void handle_shutdown_signal(int)
{
  terminate_requested = 1;
}

static void generate_worker_limit(long long max_ns, int *sec_out, int *nano_out)
{
  long long limit = max_ns;
  if (limit <= 0)
  {
    limit = 1000000000LL;
  }
  long long value = 1 + (rand() % limit);
  *sec_out = static_cast<int>(value / 1000000000LL);
  *nano_out = static_cast<int>(value % 1000000000LL);
}

static int find_free_slot(const PCB table[])
{
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    if (table[i].occupied == 0)
    {
      return i;
    }
  }
  return -1;
}

static void clear_slot(PCB table[], pid_t pid)
{
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    if (table[i].occupied && table[i].pid == pid)
    {
      table[i].occupied = 0;
      table[i].pid = 0;
      table[i].startSeconds = 0;
      table[i].startNano = 0;
      table[i].endingTimeSeconds = 0;
      table[i].endingTimeNano = 0;
      return;
    }
  }
}

static int find_slot_by_pid(const PCB table[], pid_t pid)
{
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    if (table[i].occupied && table[i].pid == pid)
    {
      return i;
    }
  }
  return -1;
}

// Spawn a single child process running the worker program.
static pid_t launch_child(int max_seconds, int max_nanos)
{
  // Fork the process; the child will exec into the user program.
  pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    return -1;
  }
  if (pid == 0)
  {
    char sec_arg[32];
    char nano_arg[32];
    snprintf(sec_arg, sizeof(sec_arg), "%d", max_seconds);
    snprintf(nano_arg, sizeof(nano_arg), "%d", max_nanos);

    // Replace child image with ./worker; only returns on error.
    execlp("./worker", "worker", sec_arg, nano_arg, static_cast<char *>(nullptr));
    perror("exec");
    exit(1);
  }
  // Parent continues here after successful fork.
  cout << "OSS: launched PID " << static_cast<int>(pid) << endl;
  return 0;
}

int main(int argc, char *argv[])
{
  signal(SIGALRM, handle_shutdown_signal);
  signal(SIGINT, handle_shutdown_signal);
  alarm(60);

  srand(static_cast<unsigned int>(time(nullptr)));

  int opt;
  int proc = 1;
  int simul = 1;
  double timelimit = 1.0;
  double interval = 0.0;

  while ((opt = getopt(argc, argv, "hn:s:t:i:")) != -1)
  {
    switch (opt)
    {
    case 'h':
      // Help flag: print usage and exit.
      print_usage(argv[0]);
      return 0;
    case 'n':
      // Total number of processes to launch.
      proc = atoi(optarg);
      break;
    case 's':
      // Maximum number of concurrent processes.
      simul = atoi(optarg);
      break;
    case 't':
      // Simulated time before oss terminates.
      timelimit = atof(optarg);
      break;
    case 'i':
      // Minimum interval between launching children (seconds).
      interval = atof(optarg);
      break;
    default:
      // Unknown flag: show usage and exit with error.
      print_usage(argv[0]);
      return 1;
    }
  }

  if (proc < 1)
  {
    // Enforce minimum process count.
    proc = 1;
  }
  if (simul < 1)
  {
    // Enforce minimum concurrency.
    simul = 1;
  }
  if (timelimit < 0.0)
  {
    timelimit = 0.0;
  }
  if (interval < 0.0)
  {
    interval = 0.0;
  }
  if (simul > proc)
  {
    // Concurrency cannot exceed total processes.
    simul = proc;
  }

  cout << "OSS starting, PID:" << static_cast<int>(getpid())
       << " PPID:" << static_cast<int>(getppid()) << endl;
  cout << "Called with:" << endl;
  cout << "-n " << proc << endl;
  cout << "-s " << simul << endl;
  cout << "-t " << fixed << setprecision(1) << timelimit << endl;
  cout << "-i " << fixed << setprecision(1) << interval << endl;

  key_t shm_key = ftok("oss.cpp", 0);
  if (shm_key <= 0)
  {
    perror("ftok");
    return 1;
  }

  int shm_id = shmget(shm_key, sizeof(int) * kClockInts, 0700 | IPC_CREAT);
  if (shm_id <= 0)
  {
    perror("shmget");
    return 1;
  }
  g_shm_id = shm_id;

  int *clock = static_cast<int *>(shmat(shm_id, 0, 0));
  if (clock == reinterpret_cast<void *>(-1))
  {
    perror("shmat");
    return 1;
  }
  g_clock = clock;

  int *sec = &(clock[0]);
  int *nano = &(clock[1]);
  *sec = 0;
  *nano = 0;

  cout << "OSS: initial clock sec=" << *sec << " nano=" << *nano << endl;

  PCB process_table[kProcessTableSize] = {};
  g_process_table = process_table;

  int launched = 0;
  int running = 0;
  int finished = 0;
  const int tick_ns = 1000000000; // 1 second per iteration.
  const int interval_ns = static_cast<int>(interval * 1000000000.0);
  const long long timelimit_ns = static_cast<long long>(timelimit * 1000000000.0);
  long long total_run_ns = 0;
  int last_launch_sec = 0;
  int last_launch_ns = 0;
  int last_table_sec = 0;
  int last_table_ns = 0;

  while (finished < proc)
  {
    if (terminate_requested)
    {
      kill_all_children();
      break;
    }
    bool can_launch = true;
    bool stop_launching = false;
    if (timelimit_ns > 0)
    {
      long long elapsed_ns = static_cast<long long>(*sec) * 1000000000LL + *nano;
      if (elapsed_ns >= timelimit_ns)
      {
        can_launch = false;
        stop_launching = true;
      }
    }
    if (interval_ns > 0)
    {
      long long since_last = (static_cast<long long>(*sec) - last_launch_sec) * 1000000000LL +
                             (static_cast<long long>(*nano) - last_launch_ns);
      if (since_last < interval_ns)
      {
        can_launch = false;
      }
    }

    if (running < simul && launched < proc && can_launch)
    {
      int slot = find_free_slot(process_table);
      if (slot >= 0)
      {
        int worker_max_seconds = 0;
        int worker_max_nanos = 0;
        generate_worker_limit(timelimit_ns, &worker_max_seconds, &worker_max_nanos);

        pid_t child_pid = launch_child(worker_max_seconds, worker_max_nanos);
        if (child_pid < 0)
        {
          shmdt(clock);
          clock = nullptr;
          shmctl(shm_id, IPC_RMID, nullptr);
          return 1;
        }
        process_table[slot].occupied = 1;
        process_table[slot].pid = child_pid;
        process_table[slot].startSeconds = *sec;
        process_table[slot].startNano = *nano;
        process_table[slot].endingTimeSeconds = *sec + worker_max_seconds;
        process_table[slot].endingTimeNano = *nano + worker_max_nanos;
        if (process_table[slot].endingTimeNano >= 1000000000)
        {
          process_table[slot].endingTimeSeconds +=
              process_table[slot].endingTimeNano / 1000000000;
          process_table[slot].endingTimeNano =
              process_table[slot].endingTimeNano % 1000000000;
        }
        running++;
        launched++;
        last_launch_sec = *sec;
        last_launch_ns = *nano;
      }
    }

    while (true)
    {
      int status = 0;
      pid_t done = waitpid(-1, &status, WNOHANG);
      if (done <= 0)
      {
        break;
      }
      int slot = find_slot_by_pid(process_table, done);
      if (slot >= 0)
      {
        long long end_ns = static_cast<long long>(*sec) * 1000000000LL + *nano;
        long long start_ns =
            static_cast<long long>(process_table[slot].startSeconds) * 1000000000LL +
            process_table[slot].startNano;
        if (end_ns > start_ns)
        {
          total_run_ns += (end_ns - start_ns);
        }
      }
      running--;
      finished++;
      clear_slot(process_table, done);
    }

    if (stop_launching && running == 0)
    {
      break;
    }

    *nano += tick_ns;
    if (*nano >= 1000000000)
    {
      *sec += 1;
      *nano -= 1000000000;
    }

    long long since_table = (static_cast<long long>(*sec) - last_table_sec) * 1000000000LL +
                            (static_cast<long long>(*nano) - last_table_ns);
    if (since_table >= 500000000LL)
    {
      print_process_table(process_table, *sec, *nano);
      last_table_sec = *sec;
      last_table_ns = *nano;
    }
  }

  cout << "OSS: final clock sec=" << *sec << " nano=" << *nano << endl;
  cout << "OSS PID:" << static_cast<int>(getpid()) << " Terminating" << endl;
  cout << launched << " workers were launched and terminated" << endl;
  long long total_sec = total_run_ns / 1000000000LL;
  long long total_ns = total_run_ns % 1000000000LL;
  cout << "Workers ran for a combined time of " << total_sec << " seconds " << total_ns
       << " nanoseconds." << endl;

  cleanup_shared_memory();

  cout << "OSS: summary launched " << launched << " finished " << finished << endl;
  return 0;
}
