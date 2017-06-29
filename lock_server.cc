// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}


lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
    std::unique_lock<std::mutex> ulock(server_mtx);
    if (used_locks.find(lid) == used_locks.end())
    {
        used_locks[lid].state = lock_state::BUSY;
    }
    else
    {
        while (used_locks.find(lid) != used_locks.end() \
            && used_locks[lid].state != lock_state::FREE)
        {
			cv.wait(ulock);
            //used_locks[lid].cv.wait(ulock);
        }
        used_locks[lid].state = lock_state::BUSY;
    }
    return lock_protocol::OK;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
    std::unique_lock<std::mutex> ulock(server_mtx);
    if (used_locks.find(lid) != used_locks.end() \
        && used_locks[lid].state != lock_state::FREE)
    {
        used_locks[lid].state = lock_state::FREE;
        //used_locks[lid].cv.notify_all();
		cv.notify_all();
    }
    return lock_protocol::OK;
}
