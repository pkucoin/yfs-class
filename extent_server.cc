// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <fstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server() {
    int ret;
    put(1, "", ret);
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  // You fill this in for Lab 2.
  std::lock_guard<std::mutex> slock(mtx);
  auto now = std::time(nullptr);
  if (data.find(id) == data.end())
  {
    dir_info di; 
    di.attr.atime = now;
    data[id] = di;
  }
  data[id].buf = std::move(buf);
  data[id].attr.mtime = data[id].attr.ctime = now;
  std::ofstream outf("debug.txt", std::ofstream::app);
  outf << "put: id:" << id << "buf:" << buf << std::endl;
  outf.close();
  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  // You fill this in for Lab 2.
    std::lock_guard<std::mutex> slock(mtx);
    if (data.find(id) == data.end())
    {
        return extent_protocol::NOENT;
    }
    buf = data[id].buf;
    data[id].attr.atime = std::time(nullptr);
  std::ofstream outf("debug.txt", std::ofstream::app);
  outf << "get: id:" << id << "buf:" << buf << std::endl;
  outf.close();
    return extent_protocol::OK;

}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You fill this in for Lab 2.
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
    if (data.find(id) == data.end())
    {
        return extent_protocol::NOENT;
    }
    a.size = data[id].buf.size();
    a.atime = data[id].attr.atime;
    a.mtime = data[id].attr.mtime;
    a.ctime = data[id].attr.ctime;
  std::ofstream outf("debug.txt", std::ofstream::app);
  outf << "getattr: id:" << id << std::endl;
  outf.close();
    return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  // You fill this in for Lab 2.
    std::lock_guard<std::mutex> slock(mtx);
    if (data.find(id) == data.end())
    {
        return extent_protocol::NOENT;
    }
    data.erase(id);
  std::ofstream outf("debug.txt", std::ofstream::app);
  outf << "remove: id:" << id << std::endl;
  outf.close();
    return extent_protocol::OK;
}

