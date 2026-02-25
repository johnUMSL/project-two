// Author: John Garrett
// Date: 2026-02-10
// Description: Launches and coordinates child processes

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using std::cerr;
using std::cout;
using std::endl;

struct PCB
{
  int occupied;          // 0 or 1
  pid_t pid;             // child pid
  int startSeconds;      // simulated time at fork
  int startNano;         // simulated time at fork
  int endingTimeSeconds; // estimated end time (start + interval)
  int endingTimeNano;    // estimated end time (start + interval)
};

static constexpr int kProcessTableSize = 20;
static constexpr int kClockInts = 2;

// Start with 10ms per loop, as suggested by the handout (adjust if needed).
static constexpr int kTickNs = 10'000'000; // 10 ms simulated per loop
static constexpr long long kHalfSecondNs = 500'000'000LL;

static volatile sig_atomic_t g_shutdown_requested = 0;

static int g_shm_id = -1;
static int *g_clock = nullptr; // points to 2 ints: [sec, nano]
static PCB *g_table_for_signal = nullptr;

static void print_usage(const char *prog)
{
  cout << "Usage: " << prog
       << " [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInSecondsToLaunchChildren]\n";
}

static inline long long clock_to_ns(int sec, int nano)
{
  return static_cast<long long>(sec) * 1'000'000'000LL + static_cast<long long>(nano);
}

static inline void ns_to_clock(long long ns, int &sec_out, int &nano_out)
{
  sec_out = static_cast<int>(ns / 1'000'000'000LL);
  nano_out = static_cast<int>(ns % 1'000'000'000LL);
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
  if (!g_table_for_signal)
    return;
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    if (g_table_for_signal[i].occupied && g_table_for_signal[i].pid > 0)
    {
      kill(g_table_for_signal[i].pid, SIGTERM);
    }
  }
}

// Signal handler: request shutdown; main loop will kill children and cleanup.
static void handle_signal(int)
{
  g_shutdown_requested = 1;
}

static int find_free_slot(PCB table[])
{
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    if (table[i].occupied == 0)
      return i;
  }
  return -1;
}

static int find_slot_by_pid(PCB table[], pid_t pid)
{
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    if (table[i].occupied && table[i].pid == pid)
      return i;
  }
  return -1;
}

static void clear_slot(PCB table[], int idx)
{
  table[idx].occupied = 0;
  table[idx].pid = 0;
  table[idx].startSeconds = 0;
  table[idx].startNano = 0;
  table[idx].endingTimeSeconds = 0;
  table[idx].endingTimeNano = 0;
}

static void print_process_table(const PCB table[], int sec, int nano)
{
  cout << "OSS PID:" << static_cast<int>(getpid())
       << " SysClockS: " << sec
       << " SysclockNano: " << nano << "\n";
  cout << "Process Table:\n";
  cout << "Entry Occupied PID StartS StartN EndingTimeS EndingTimeNano\n";
  for (int i = 0; i < kProcessTableSize; ++i)
  {
    cout << std::setw(5) << i << " "
         << std::setw(8) << table[i].occupied << " "
         << std::setw(5) << static_cast<int>(table[i].pid) << " "
         << std::setw(6) << table[i].startSeconds << " "
         << std::setw(6) << table[i].startNano << " "
         << std::setw(11) << table[i].endingTimeSeconds << " "
         << std::setw(14) << table[i].endingTimeNano << "\n";
  }
  cout.flush();
}

// Choose a random worker interval up to max_ns (at least 1ns, up to max_ns).
static void random_worker_interval(long long max_ns, int &sec_out, int &nano_out)
{
  if (max_ns <= 0)
    max_ns = 1'000'000'000LL; // default 1s if user gave 0
  long long r = 1 + (static_cast<long long>(rand()) % max_ns);
  ns_to_clock(r, sec_out, nano_out);
}

static pid_t launch_worker(int interval_sec, int interval_ns)
{
  pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    return -1;
  }
  if (pid == 0)
  {
    char sec_arg[32], ns_arg[32];
    std::snprintf(sec_arg, sizeof(sec_arg), "%d", interval_sec);
    std::snprintf(ns_arg, sizeof(ns_arg), "%d", interval_ns);

    execlp("./worker", "worker", sec_arg, ns_arg, (char *)nullptr);
    perror("exec");
    _exit(1);
  }
  cout << "OSS: launched PID " << static_cast<int>(pid) << "\n";
  return pid;
}

int main(int argc, char *argv[])
{
  // 60 real-second cap and Ctrl-C handling.
  signal(SIGALRM, handle_signal);
  signal(SIGINT, handle_signal);
  alarm(60);

  srand(static_cast<unsigned int>(time(nullptr)));

  int opt;
  int n_total = 1;         // -n
  int s_simul = 1;         // -s
  double t_limit = 1.0;    // -t (simulated seconds)
  double i_interval = 0.0; // -i (simulated seconds between launches)

  while ((opt = getopt(argc, argv, "hn:s:t:i:")) != -1)
  {
    switch (opt)
    {
    case 'h':
      print_usage(argv[0]);
      return 0;
    case 'n':
      n_total = std::atoi(optarg);
      break;
    case 's':
      s_simul = std::atoi(optarg);
      break;
    case 't':
      t_limit = std::atof(optarg);
      break;
    case 'i':
      i_interval = std::atof(optarg);
      break;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  if (n_total < 1)
    n_total = 1;
  if (s_simul < 1)
    s_simul = 1;
  if (s_simul > n_total)
    s_simul = n_total;
  if (t_limit < 0.0)
    t_limit = 0.0;
  if (i_interval < 0.0)
    i_interval = 0.0;

  cout << "OSS starting, PID:" << static_cast<int>(getpid())
       << " PPID:" << static_cast<int>(getppid()) << "\n";
  cout << "Called with:\n";
  cout << "-n " << n_total << "\n";
  cout << "-s " << s_simul << "\n";
  cout << "-t " << std::fixed << std::setprecision(1) << t_limit << "\n";
  cout << "-i " << std::fixed << std::setprecision(1) << i_interval << "\n";

  // Shared memory setup (clock = 2 ints)
  // Use a stable ftok path that both oss and worker can use: current directory.
  key_t shm_key = ftok(".", 'A');
  if (shm_key == -1)
  {
    perror("ftok");
    return 1;
  }

  g_shm_id = shmget(shm_key, sizeof(int) * kClockInts, IPC_CREAT | 0666);
  if (g_shm_id == -1)
  {
    perror("shmget");
    return 1;
  }

  g_clock = static_cast<int *>(shmat(g_shm_id, nullptr, 0));
  if (g_clock == reinterpret_cast<void *>(-1))
  {
    perror("shmat");
    g_clock = nullptr;
    cleanup_shared_memory();
    return 1;
  }

  int &sec = g_clock[0];
  int &nano = g_clock[1];
  sec = 0;
  nano = 0;

  cout << "OSS: initial clock sec=" << sec << " nano=" << nano << "\n";

  PCB processTable[kProcessTableSize]{};
  g_table_for_signal = processTable;

  const long long t_limit_ns = static_cast<long long>(t_limit * 1'000'000'000.0);
  const long long i_interval_ns = static_cast<long long>(i_interval * 1'000'000'000.0);

  int launched = 0;
  int running = 0;
  int finished = 0;

  long long total_run_ns = 0;

  // For interval logic (first launch should not be blocked).
  bool launched_any = false;
  long long last_launch_time_ns = 0;

  // For printing every 0.5 simulated seconds.
  long long last_table_print_ns = 0;

  // Main loop: while (stillChildrenToLaunch || haveChildrenInSystem)
  while (launched < n_total || running > 0)
  {
    if (g_shutdown_requested)
    {
      // Kill children then break; then cleanup.
      kill_all_children();
      // Reap any remaining children.
      while (waitpid(-1, nullptr, WNOHANG) > 0)
      {
      }
      break;
    }

    // incrementClock();
    nano += kTickNs;
    if (nano >= 1'000'000'000)
    {
      sec += 1;
      nano -= 1'000'000'000;
    }

    const long long now_ns = clock_to_ns(sec, nano);

    // Every half a second of simulated clock time, output the process table
    if (now_ns - last_table_print_ns >= kHalfSecondNs)
    {
      print_process_table(processTable, sec, nano);
      last_table_print_ns = now_ns;
    }

    // checkIfChildHasTerminated(); (nonblocking)
    while (true)
    {
      int status = 0;
      pid_t done = waitpid(-1, &status, WNOHANG);
      if (done <= 0)
        break;

      int idx = find_slot_by_pid(processTable, done);
      if (idx >= 0)
      {
        long long start_ns = clock_to_ns(processTable[idx].startSeconds, processTable[idx].startNano);
        long long end_ns = now_ns;
        if (end_ns > start_ns)
          total_run_ns += (end_ns - start_ns);
        clear_slot(processTable, idx);
      }
      running--;
      finished++;
    }

    // possiblyLaunchNewChild(obeying process limits and time bound limits)
    const bool stillChildrenToLaunch = (launched < n_total);
    const bool underSimulLimit = (running < s_simul);

    // Stop launching new children after simulated time reaches -t (if t_limit_ns > 0)
    bool time_allows_launch = true;
    if (t_limit_ns > 0 && now_ns >= t_limit_ns)
    {
      time_allows_launch = false;
    }

    bool interval_allows_launch = true;
    if (i_interval_ns > 0 && launched_any)
    {
      if (now_ns - last_launch_time_ns < i_interval_ns)
      {
        interval_allows_launch = false;
      }
    }

    if (stillChildrenToLaunch && underSimulLimit && time_allows_launch && interval_allows_launch)
    {
      int slot = find_free_slot(processTable);
      if (slot >= 0)
      {
        // Pass worker a random interval (sec,nano). We choose up to t_limit_ns (or 1s default).
        int w_sec = 0, w_ns = 0;
        random_worker_interval(t_limit_ns, w_sec, w_ns);

        pid_t child = launch_worker(w_sec, w_ns);
        if (child > 0)
        {
          processTable[slot].occupied = 1;
          processTable[slot].pid = child;
          processTable[slot].startSeconds = sec;
          processTable[slot].startNano = nano;

          // estimated end time = start + worker interval
          long long end_est_ns = now_ns + clock_to_ns(w_sec, w_ns);
          ns_to_clock(end_est_ns, processTable[slot].endingTimeSeconds, processTable[slot].endingTimeNano);

          launched++;
          running++;
          launched_any = true;
          last_launch_time_ns = now_ns;
        }
      }
    }

    // Loop continues; oss terminates after all workers have finished (or real-time shutdown).
  }

  cout << "OSS: final clock sec=" << sec << " nano=" << nano << "\n";
  cout << "OSS PID:" << static_cast<int>(getpid()) << " Terminating\n";
  cout << finished << " workers were launched and terminated\n";
  cout << "Workers ran for a combined time of "
       << (total_run_ns / 1'000'000'000LL) << " seconds "
       << (total_run_ns % 1'000'000'000LL) << " nanoseconds.\n";

  cleanup_shared_memory();
  return 0;
}