// Author: John Garrett
// Date: 2026-02-10
// Description: Launches and coordinates child processes with configurable limits (stub).

#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

static const int kClockInts = 2;

static void print_usage(const char *prog)
{
  // Print valid CLI usage for this program.
  cout << "Usage: " << prog << " [-h] [-n proc] [-s simul] [-t timelimit] [-i interval]" << endl;
}

// Spawn a single child process running the worker program.
static int launch_child()
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
    // Replace child image with ./worker; only returns on error.
    execlp("./worker", "worker", static_cast<char *>(nullptr));
    perror("exec");
    exit(1);
  }
  // Parent continues here after successful fork.
  cout << "OSS: launched PID " << static_cast<int>(pid) << endl;
  return 0;
}

int main(int argc, char *argv[])
{
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
  if (proc > 15)
  {
    // Enforce maximum process count.
    proc = 15;
  }
  if (simul < 1)
  {
    // Enforce minimum concurrency.
    simul = 1;
  }
  if (simul > 15)
  {
    // Enforce maximum concurrency.
    simul = 15;
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

  int *clock = static_cast<int *>(shmat(shm_id, 0, 0));
  if (clock == reinterpret_cast<void *>(-1))
  {
    perror("shmat");
    return 1;
  }

  int *sec = &(clock[0]);
  int *nano = &(clock[1]);
  *sec = 0;
  *nano = 0;

  cout << "OSS: initial clock sec=" << *sec << " nano=" << *nano << endl;

  int running = 0;
  int launched = 0;
  int finished = 0;

  // Start initial batch up to the concurrent limit.
  while (running < simul && launched < proc)
  {
    if (launch_child() == 0)
    {
      running++;
      launched++;
    }
  }

  // Maintain the concurrency window until all processes are launched.
  while (launched < proc)
  {
    // Block until any child exits.
    wait(nullptr);
    running--;
    finished++;
    if (launch_child() == 0)
    {
      running++;
      launched++;
    }
  }

  // Wait for remaining children to finish.
  while (running > 0)
  {
    // Drain remaining children.
    wait(nullptr);
    running--;
    finished++;
  }

  cout << "OSS: final clock sec=" << *sec << " nano=" << *nano << endl;

  shmdt(clock);
  clock = nullptr;
  shmctl(shm_id, IPC_RMID, nullptr);

  cout << "OSS: summary launched " << launched << " finished " << finished << endl;
  return 0;
}

// 0 < -s < 15
// 0 < -n < 100
// -t >= 0
