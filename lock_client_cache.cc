// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

lock_client_cache::lock_client_cache(std::string xdst, 
    class lock_release_user *_lu) : lock_client(xdst), lu(_lu)
{
    rpcs *rlsrpc = new rpcs(0);
    rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
    rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

    const char *hname;
    hname = "127.0.0.1";
    std::ostringstream host;
    host << hname << ":" << rlsrpc->port();
    id = host.str();
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
    // ensure exclusive access to lock_cache 
    // until we get and lock lock_cache[lid]
    std::unique_lock<std::mutex> map_lock(mtx_map); 
    auto itr = lock_cache.find(lid);
    std::shared_ptr<client_lock> cur_lock;
    if (itr == lock_cache.end())
    {
        cur_lock = std::make_shared<client_lock>(lid);
        lock_cache[lid] = cur_lock;
    }
    else
    {
        cur_lock = itr->second;
    }
    
    std::unique_lock<std::mutex> single_lock(cur_lock->mtx); 
    // we will never erase a kv in lock_cache in this program
    // so cur_lock will always be valid (insert or erase other kv won't invalidate cur_lock)
    // so we no longer need exclusive access to lock_cache
    map_lock.unlock(); 
    //while (cur_lock->status != client_lock::NONE 
    //        && cur_lock->status != client_lock::FREE)
    while (cur_lock->status == client_lock::LOCKED 
            || cur_lock->status == client_lock::ACQUIRING)
    {
        // wait until cur_lock->status is (possibly) available, i.e. NONE or FREE
        cur_lock->available_cv.wait(single_lock);
    }
    while (cur_lock->status == client_lock::RELEASING)
    {
        // wait while release
        cur_lock->release_cv.wait(single_lock);
    }
    if (cur_lock->status == client_lock::FREE)
    {
        // the lock is free, so grant it
        cur_lock->status = client_lock::LOCKED;
        return lock_protocol::OK;
    }
    else if (cur_lock->status == client_lock::NONE)
    {
        // should not keep mutex locked while waiting for rpc's ret
        // since rpc could take a long time. 
        // moreover it may lead to distributed deadlock
        cur_lock->status = client_lock::ACQUIRING;
        while (true)
        {
            single_lock.unlock(); 
            int r;
            auto ret = cl->call(lock_protocol::acquire, lid, id, r);  
            single_lock.lock(); // lock again
            if (ret == lock_protocol::OK)
            {
                cur_lock->status = client_lock::LOCKED;
                return lock_protocol::OK;
            }
            else if (ret == lock_protocol::RETRY)
            {
                // RETRY doesn't mean we should try again and again immediately
                // instead we wait for a explicit notification
                // i.e. a retry rpc
                // pay attention that retry rpc may arrive before we got this RETRY ret
                if (cur_lock->num_retry)
                {
                    cur_lock->num_retry = 0;
                    continue;
                }
                while (cur_lock->num_retry == 0)
                {
                    cur_lock->retry_cv.wait(single_lock);
                }
                cur_lock->num_retry = 0;
            }
            else
            {
                return ret;
            }
        }
    }
    return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
    std::unique_lock<std::mutex> map_lock(mtx_map);
    auto itr = lock_cache.find(lid);
    if (itr == lock_cache.end())
    {
        return lock_protocol::OK;
    }

    auto cur_lock = itr->second;
    map_lock.unlock();
    std::unique_lock<std::mutex> single_lock(cur_lock->mtx);
    if (cur_lock->status != client_lock::LOCKED)
    {
        return lock_protocol::IOERR;
    }
    if (cur_lock->num_revoke)
    {
        lu->dorelease(lid);
        cur_lock->num_revoke = 0; 
        cur_lock->status = client_lock::RELEASING;
        int r;
        single_lock.unlock();
        auto ret = cl->call(lock_protocol::release, lid, id, r);
        single_lock.lock();
        if (ret == lock_protocol::OK)
        {
            cur_lock->status = client_lock::NONE;
            cur_lock->available_cv.notify_all();
            cur_lock->release_cv.notify_all();
        }
        else
        {
            return ret;
        }
    }
    else
    {
        cur_lock->status = client_lock::FREE;
        cur_lock->available_cv.notify_one();
    }

    return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
        int &)
{
    int ret = rlock_protocol::OK;
    std::unique_lock<std::mutex> map_lock(mtx_map);
    auto itr = lock_cache.find(lid);
    if (itr == lock_cache.end())
    {
        return ret;
    }

    auto cur_lock = itr->second;
    std::unique_lock<std::mutex> single_lock(cur_lock->mtx);
    map_lock.unlock();
    if (cur_lock->status == client_lock::FREE)
    {
        // the lock is free, so give it back
        lu->dorelease(lid);
        cur_lock->status = client_lock::RELEASING;
        int r;
        single_lock.unlock();
        auto ret = cl->call(lock_protocol::release, lid, id, r);
        single_lock.lock();
        if (ret == lock_protocol::OK)
        {
            cur_lock->status = client_lock::NONE;
            cur_lock->available_cv.notify_all();
            cur_lock->release_cv.notify_all();
        }
        else
        {
            cur_lock->status = client_lock::FREE;
            return ret;
        }
    }
    else
    {
        // mark it
        cur_lock->num_revoke++;
    }

    return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
        int &)
{
    int ret = rlock_protocol::OK;
    std::unique_lock<std::mutex> map_lock(mtx_map);
    auto itr = lock_cache.find(lid);
    if (itr == lock_cache.end())
    {
        return ret;
    }

    auto cur_lock = itr->second;
    std::unique_lock<std::mutex> single_lock(cur_lock->mtx);
    map_lock.unlock();

    cur_lock->num_retry++;
    cur_lock->retry_cv.notify_one();
    return ret;
}



