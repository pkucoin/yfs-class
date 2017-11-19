// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
    sockaddr_in dstsock;
    make_sockaddr(dst.c_str(), &dstsock);
    cl = new rpcc(dstsock);
    if (cl->bind() != 0) {
        printf("extent_client: bind failed\n");
    }
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
    extent_protocol::status ret = extent_protocol::OK;
    std::unique_lock<std::mutex> ulock(cache_mtx);
    if (cache_map.find(eid) == cache_map.end() || (!cache_map[eid].removed && cache_map[eid].attr_only))
    {
        ulock.unlock();
        ret = cl->call(extent_protocol::get, eid, buf);
        extent_protocol::attr attr;
        getattr(eid, attr);
        ulock.lock();
        cache_map[eid] = { buf, attr, false, false, false };
    }
    else
    {
        buf = cache_map[eid].data;
    }
    cache_map[eid].attr.atime = std::time(nullptr);
    return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
        extent_protocol::attr &attr)
{
    extent_protocol::status ret = extent_protocol::OK;
    std::unique_lock<std::mutex> ulock(cache_mtx);
    if (cache_map.find(eid) == cache_map.end())
    {
        ulock.unlock();
        ret = cl->call(extent_protocol::getattr, eid, attr);
        ulock.lock();
        cache_map[eid] = { "", attr, false, true, false };
    }
    else
    {
        attr = cache_map[eid].attr;
    }
    return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
    extent_protocol::status ret = extent_protocol::OK;
    std::unique_lock<std::mutex> ulock(cache_mtx);
    if (cache_map.find(eid) == cache_map.end() || cache_map[eid].removed || cache_map[eid].attr_only)
    {
        extent_protocol::attr attr;
        attr.size = buf.size();
        attr.ctime = attr.mtime = attr.atime = std::time(nullptr);
        cache_map[eid] = { buf, attr, true, false, false };
    }
    else
    {
        cache_map[eid].data = buf;
        cache_map[eid].attr.size = buf.size();
        cache_map[eid].attr.mtime = cache_map[eid].attr.ctime = std::time(nullptr);
        cache_map[eid].dirty = true;
        cache_map[eid].attr_only = false;
        cache_map[eid].removed = false;
    }
    return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
    extent_protocol::status ret = extent_protocol::OK;
    std::unique_lock<std::mutex> ulock(cache_mtx);
    cache_map[eid].data = "";
    cache_map[eid].removed = true;
    ulock.unlock();
    
    //int r;
    //ret = cl->call(extent_protocol::remove, eid, r);
    return ret;
}

extent_protocol::status
extent_client::flush(extent_protocol::extentid_t eid)
{
    extent_protocol::status ret = extent_protocol::OK;
    std::unique_lock<std::mutex> ulock(cache_mtx);
    if (cache_map.find(eid) != cache_map.end())
    {
        if (!cache_map[eid].attr_only)
        {
            if (cache_map[eid].dirty)
            {
                ulock.unlock();
                int r;
                ret = cl->call(extent_protocol::put, eid, cache_map[eid].data, r);
                ulock.lock();
            }

            if (cache_map[eid].removed)
            {
                ulock.unlock();
                int r;
                ret = cl->call(extent_protocol::remove, eid, r);
                ulock.lock();
            }

        }
        cache_map.erase(eid);
    }
    return ret;
}
