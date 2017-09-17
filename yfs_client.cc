// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <random>
#include <chrono>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  generator = std::mt19937(std::random_device()());
  uid = std::uniform_int_distribution<int>(0, int((long long)(1 << 31) - 1));
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

yfs_client::inum
yfs_client::rand_inum()
{
	return uid(generator) | 0x80000000;
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

yfs_client::status
yfs_client::create(inum dir, const char *name, inum &ret_id)
{
    if (isdir(dir))
    {
        std::string buf;
        auto ret = ec->get(dir, buf);
        if (ret != OK)
        {
            return NOENT;
        }
		dirent_list dl(buf);        
        if (dl.match(name))
        {
            return EXIST;
        }
		ret_id = rand_inum();
		dl.add(ret_id, name);
        std::ofstream outf("debug.txt", std::ofstream::app);
        outf << "created dir:" << dir << " content:" << dl.to_string() << std::endl;
        outf.close();
        ret = ec->put(ret_id, "");
        if (ret != OK) return ret;
        return ec->put(dir, dl.to_string());
    }

    return NOENT;
}

yfs_client::status
yfs_client::lookup(inum dir, const char *name, inum &ret_id)
{
    if (isdir(dir))
    {
        std::string buf;
        auto ret = ec->get(dir, buf);
        if (ret != OK)
        {
            return IOERR;
        }
		dirent_list dl(buf);
        if (!dl.match(name))
        {
            return NOENT;
        }
        ret_id = dl.get(name);
        std::ofstream outf("debug.txt", std::ofstream::app);
        outf << "lookup dir" << dir << " name:" << name << " return:" << ret_id << std::endl;
        outf.close();
        return OK;
    }
    
    return IOERR;
}

yfs_client::status
yfs_client::readdir(inum dir, std::unordered_map<std::string, inum>& ret_map)
{
    if (isdir(dir))
    {
        std::string buf;
        auto ret = ec->get(dir, buf);
        if (ret != OK)
        {
            return IOERR;
        }
		dirent_list dl(buf);
        ret_map = dl.get_map();
        std::ofstream outf("debug.txt", std::ofstream::app);
        outf << "readdir dir:" << dir << " return:" << dl.to_string() << std::endl;
        outf.close();
        return OK;
    }
    
    return IOERR;
}
