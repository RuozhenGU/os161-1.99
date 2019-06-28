#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
/*
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/*
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */

int transDir(Direction o);
volatile int waitlist[4] = {-1,-1,-1,-1};
volatile int len_waitlist = 0;

static struct lock *lk_mutex;
static struct cv *cv_north;
static struct cv *cv_south;
static struct cv *cv_east;
static struct cv *cv_west;

static volatile int currentTraffic = -1;
static volatile int count = 0;
/*
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 *
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  lk_mutex = lock_create("ls_mutex");
  cv_north = cv_create("cv_north");
  cv_south = cv_create("cv_south");
  cv_west = cv_create("cv_west");
  cv_east = cv_create("cv_east");
  if (cv_north == NULL || cv_south == NULL || cv_west == NULL || cv_east == NULL) {
    panic("could not create condition variable");
  } else if(lk_mutex == NULL){
    panic("could not create lock");
  }
}

/*
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{

  /* replace this default implementation with your own implementation */
  KASSERT(cv_north != NULL && cv_east != NULL && cv_west != NULL && cv_south != NULL);
  KASSERT(lk_mutex != NULL);

  cv_destroy(cv_north);
  cv_destroy(cv_south);
  cv_destroy(cv_east);
  cv_destroy(cv_west);
  lock_destroy(lk_mutex);

}

int transDir(Direction o) {
  if (o == north) return 0;
  if (o == east) return 1;
  if (o == south) return 2;
  if (o == west) return 3;
  return -1;
}
/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination)
{
  lock_acquire(lk_mutex);
  if(count == 0 && (currentTraffic == -1 || currentTraffic == transDir(origin))) {
    count++;
    currentTraffic = transDir(origin);
  } else {
    int flag = 0;
    if (len_waitlist != 4){
      for(int i = 0; i < 4; i++) {
        if (waitlist[i] == transDir(origin)) flag = 1;
      }
      if (flag == 0){
        waitlist[len_waitlist] = transDir(origin);
        len_waitlist++;
      }
    }
    if (origin == north) {
      cv_wait(cv_north, lk_mutex);
    } else if (origin == south) {
      cv_wait(cv_south, lk_mutex);
    } else if (origin == west) {
      cv_wait(cv_west, lk_mutex);
    } else if (origin == east) {
      cv_wait(cv_east, lk_mutex);
    }

    count++;
  }
  lock_release(lk_mutex);
  (void)destination;
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination)
{
  lock_acquire(lk_mutex);

  count--;

  if(count == 0) {
    currentTraffic = waitlist[0];
    if(waitlist[0] == -1) {
      currentTraffic = -1;
    } else {
      currentTraffic = waitlist[0];
      for(int i = 0; i < 3; i++) {
        waitlist[i] = waitlist[i+1];
      }
      waitlist[3] = -1;
      len_waitlist--;
    }
    if (currentTraffic == 0) cv_broadcast(cv_north, lk_mutex);
    if (currentTraffic == 1) cv_broadcast(cv_east, lk_mutex);
    if (currentTraffic == 2) cv_broadcast(cv_south, lk_mutex);
    if (currentTraffic == 3) cv_broadcast(cv_west, lk_mutex);
  }

  lock_release(lk_mutex);
  (void)destination;
  (void)origin;
}
