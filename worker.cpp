// Author: John Garrett
// Date: 2026-02-24
// Description: Worker process for project 2 simulated clock.

#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

static const int kClockInts = 2;

int main(int argc, char *argv[])
{
  if (argc < 3)
  {
  std::cerr << "Usage: " << argv[0] << " <maxSeconds> <maxNano>" << std::endl;
    return 1;
  }

  int max_seconds = atoi(argv[1]);
  int max_nanos = atoi(argv[2]);
  if (max_seconds < 0)
  {
    max_seconds = 0;
  }
  if (max_nanos < 0)
  {
    max_nanos = 0;
  }

  key_t shm_key = ftok("oss.cpp", 0);
  if (shm_key <= 0)
  {
    perror("ftok");
    return 1;
  }

  int shm_id = shmget(shm_key, sizeof(int) * kClockInts, 0700);
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

  std::cout << "Worker starting, PID:" << static_cast<int>(getpid())
            << " PPID:" << static_cast<int>(getppid()) << std::endl;
  std::cout << "Called with:" << std::endl;
  std::cout << "Interval: " << max_seconds << " seconds, " << max_nanos << " nanoseconds" << std::endl;

  int term_sec = *sec + max_seconds;
  int term_nano = *nano + max_nanos;
  if (term_nano >= 1000000000)
  {
    term_sec += term_nano / 1000000000;
    term_nano = term_nano % 1000000000;
  }

  std::cout << "WORKER PID:" << static_cast<int>(getpid())
            << " PPID:" << static_cast<int>(getppid()) << std::endl;
  std::cout << "SysClockS: " << *sec << " SysclockNano: " << *nano
            << " TermTimeS: " << term_sec << " TermTimeNano: " << term_nano << std::endl;
  std::cout << "--Just Starting" << std::endl;

  int start_sec = *sec;
  int last_reported_sec = *sec;
  while (true)
  {
    int cur_sec = *sec;
    int cur_nano = *nano;

    if (cur_sec > term_sec || (cur_sec == term_sec && cur_nano >= term_nano))
    {
      std::cout << "WORKER PID:" << static_cast<int>(getpid())
                << " PPID:" << static_cast<int>(getppid()) << std::endl;
      std::cout << "SysClockS: " << cur_sec << " SysclockNano: " << cur_nano
                << " TermTimeS: " << term_sec << " TermTimeNano: " << term_nano << std::endl;
      std::cout << "--Terminating" << std::endl;
      break;
    }

    if (cur_sec != last_reported_sec)
    {
      int elapsed = cur_sec - start_sec;
      std::cout << "WORKER PID:" << static_cast<int>(getpid())
                << " PPID:" << static_cast<int>(getppid()) << std::endl;
      std::cout << "SysClockS: " << cur_sec << " SysclockNano: " << cur_nano
                << " TermTimeS: " << term_sec << " TermTimeNano: " << term_nano << std::endl;
      std::cout << "--" << elapsed << " seconds have passed since starting" << std::endl;
      last_reported_sec = cur_sec;
    }
  }

  shmdt(clock);
  clock = nullptr;
  return 0;
}
