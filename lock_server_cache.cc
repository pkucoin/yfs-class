// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
    int &)
{
    std::unique_lock<std::mutex> map_lock(server_mtx);
    auto itr = lock_cache.find(lid);
    std::shared_ptr<server_lock> cur_lock;
    if (itr == lock_cache.end())
    {
        cur_lock = std::make_shared<server_lock>(id);
        cur_lock->status = server_lock::LOCKED;
        cur_lock->client_id = id;
        lock_cache[lid] = cur_lock; 
        return lock_protocol::OK;
    }
    else
    {
        cur_lock = itr->second; 
    }
    std::unique_lock<std::mutex> single_lock(cur_lock->mtx);
    map_lock.unlock();
    if (cur_lock->status == server_lock::FREE)
    {
        cur_lock->status = server_lock::LOCKED;
        cur_lock->client_id = id;
        if (!cur_lock->waiting_cids.empty())
        {
            handle cl_handle(id);
            rpcc *cl = cl_handle.safebind();
            if (cl)
            {
                int r;
                cl->call(rlock_protocol::revoke, lid, r);
            }
        }
        return lock_protocol::OK;
    }
    else if (cur_lock->status == server_lock::LOCKED)
    {
        // If lock is server_lock::LOCKED, it means another client holds it AND
        // we are the first one to ask for it
        // so use revoke rpc to tell the lock owner to give back the lock
        handle cl_handle(cur_lock->client_id);
        rpcc *cl = cl_handle.safebind();
        if (cl)
        {
            int r;
            cur_lock->status = server_lock::REVOKE_SENT;
            cur_lock->waiting_cids.push(id);
            single_lock.unlock();
            cl->call(rlock_protocol::revoke, lid, r);
            single_lock.lock();
            /* it seems this is overthinking
            while (ret != lock_protocol::OK)
            {
                // If due to any unknown reason, the revoke call fails
                // we need to try again until it's OK
                // otherwise, with no more revoke the lock would not be returned
                ret = cl->call(lock_protocol::revoke, lid, r);
            }
            */
            return lock_protocol::RETRY;
        }
        else
        {
            // rpc conn fails 
            return lock_protocol::RPCERR;
        }
    }
    else if (cur_lock->status == server_lock::REVOKE_SENT)
    {
        // server_lock::REVOKE_SENT means that another client holds the lock AND
        // there have been other clients asking and waiting for it AND
        // a revoke has been sent
        cur_lock->waiting_cids.push(id);
        return lock_protocol::RETRY;
    }
    return lock_protocol::OK;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
    int &r)
{ 
    lock_protocol::status ret = lock_protocol::OK;
    std::unique_lock<std::mutex> map_lock(server_mtx);
    auto itr = lock_cache.find(lid);
    if (itr == lock_cache.end())
    {
        return lock_protocol::IOERR;
    }

    auto cur_lock = itr->second;
    std::unique_lock<std::mutex> single_lock(cur_lock->mtx);
    map_lock.unlock();
    if (cur_lock->client_id != id || cur_lock->status == server_lock::FREE)
    {
        return ret;
    }
    cur_lock->status = server_lock::FREE;
    if (!cur_lock->waiting_cids.empty())
    {
        auto waiting_cid = cur_lock->waiting_cids.front();
        cur_lock->waiting_cids.pop();
        handle cl_handler(waiting_cid);
        rpcc *cl = cl_handler.safebind();
        if (cl)
        {
            int r;
            single_lock.unlock();
            auto ret = cl->call(rlock_protocol::retry, lid, r);
            single_lock.lock();
            if (ret != lock_protocol::OK)
            {
                return ret;
            }
        }
        else
        {
            return lock_protocol::IOERR;
        }
    }
    return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
    tprintf("stat request\n");
    r = nacquire;
    return lock_protocol::OK;
}

