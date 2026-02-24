// Author: John Garrett
// Date: 2026-02-24
// Description: Worker process for project 2 simulated clock (read-only).

#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

static const int kClockInts = 2;

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

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

  std::cout << "WORKER PID:" << static_cast<int>(getpid())
            << " PPID:" << static_cast<int>(getppid()) << std::endl;
  std::cout << "WORKER: clock sec=" << *sec << " nano=" << *nano << std::endl;

  shmdt(clock);
  clock = nullptr;
  return 0;
}
