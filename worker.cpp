// Author: John Garrett
// Date: 2026-02-24
// Description: Worker process for project 2 simulated clock.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

int main(int argc, char *argv[])
{
  if (argc < 3)
  {
    fprintf(stderr, "Usage: %s <seconds> <nanoseconds>\n", argv[0]);
    return 1;
  }

  const int interval_sec = atoi(argv[1]);
  const int interval_ns = atoi(argv[2]);

  // First output what it was called with + PID/PPID (per handout).
  cout << "Worker starting, PID:" << static_cast<int>(getpid())
       << " PPID:" << static_cast<int>(getppid()) << "\n";
  cout << "Called with:\n";
  cout << "Interval: " << interval_sec << " seconds, " << interval_ns << " nanoseconds\n";

  // Attach to shared memory (same ftok as oss).
  key_t shm_key = ftok(".", 'A');
  if (shm_key == -1)
  {
    perror("ftok");
    return 1;
  }

  int shm_id = shmget(shm_key, sizeof(int) * 2, 0666);
  if (shm_id == -1)
  {
    perror("shmget");
    return 1;
  }

  int *clock = static_cast<int *>(shmat(shm_id, nullptr, 0));
  if (clock == reinterpret_cast<void *>(-1))
  {
    perror("shmat");
    return 1;
  }

  int startS = clock[0];
  int startN = clock[1];

  // Compute termination time = current clock + interval (normalize nanos)
  int termS = startS + interval_sec;
  int termN = startN + interval_ns;
  if (termN >= 1'000'000'000)
  {
    termS += termN / 1'000'000'000;
    termN = termN % 1'000'000'000;
  }
  if (termN < 0)
    termN = 0; // defensive

  cout << "WORKER PID:" << static_cast<int>(getpid())
       << " PPID:" << static_cast<int>(getppid()) << "\n";
  cout << "SysClockS: " << startS
       << " SysclockNano: " << startN
       << " TermTimeS: " << termS
       << " TermTimeNano: " << termN << "\n";
  cout << "--Just Starting\n";
  cout.flush();

  // Loop: check clock until >= term time. Output each time seconds changes.
  int last_seen_sec = startS;

  while (true)
  {
    int curS = clock[0];
    int curN = clock[1];

    // Termination check (>=)
    if (curS > termS || (curS == termS && curN >= termN))
    {
      cout << "WORKER PID:" << static_cast<int>(getpid())
           << " PPID:" << static_cast<int>(getppid()) << "\n";
      cout << "SysClockS: " << curS
           << " SysclockNano: " << curN
           << " TermTimeS: " << termS
           << " TermTimeNano: " << termN << "\n";
      cout << "--Terminating\n";
      cout.flush();
      break;
    }

    // Periodic output: every time seconds change
    if (curS != last_seen_sec)
    {
      int elapsed = curS - startS;
      if (elapsed < 0)
        elapsed = 0;

      cout << "WORKER PID:" << static_cast<int>(getpid())
           << " PPID:" << static_cast<int>(getppid()) << "\n";
      cout << "SysClockS: " << curS
           << " SysclockNano: " << curN
           << " TermTimeS: " << termS
           << " TermTimeNano: " << termN << "\n";
      cout << "--" << elapsed << " seconds have passed since starting\n";
      cout.flush();

      last_seen_sec = curS;
    }
  }

  shmdt(clock);
  return 0;
}
