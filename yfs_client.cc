// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client.h"
#include <sstream>
#include <iostream>
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
  lc = new lock_client(lock_dst);
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
yfs_client::rand_inum(bool is_dir)
{
    if (is_dir)
        return uid(generator) & 0x7FFFFFFF;
    else
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
  // You modify this function for Lab 3
  // - hold and release the file lock

  raii_wrapper rw(lc, inum);
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
  // You modify this function for Lab 3
  // - hold and release the directory lock

  raii_wrapper rw(lc, inum);
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
yfs_client::create(inum dir, const char *name, bool is_dir, inum &ret_id)
{
    if (isdir(dir))
    {
        raii_wrapper rw(lc, dir);
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
		ret_id = rand_inum(is_dir);
		dl.add(ret_id, name);
        raii_wrapper rw2(lc, ret_id);
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
        raii_wrapper rw(lc, dir);
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
        return OK;
    }
    
    return IOERR;
}

yfs_client::status
yfs_client::readdir(inum dir, std::unordered_map<std::string, inum>& ret_map)
{
    if (isdir(dir))
    {
        raii_wrapper rw(lc, dir);
        std::string buf;
        auto ret = ec->get(dir, buf);
        if (ret != OK)
        {
            return IOERR;
        }
		dirent_list dl(buf);
        ret_map = dl.get_map();
        return OK;
    }
    
    return IOERR;
}

yfs_client::status
yfs_client::setattr(inum ino, unsigned int len)
{
    if (!isfile(ino)) return NOENT;
    raii_wrapper rw(lc, ino);
    std::string buf;
    auto ret = ec->get(ino, buf);
    if (ret != OK) return ret;
    if (buf.size() > len)
    {
        buf = std::move(buf.substr(0, len));
    }
    else
    {
        buf.append(std::string(len - buf.size(), '\0'));
    }
    return ec->put(ino, buf);
}

yfs_client::status
yfs_client::read(inum ino, std::size_t off, std::size_t len, std::string& data)
{ 
    if (!isfile(ino)) return NOENT;
    raii_wrapper rw(lc, ino);
    std::string buf;
    auto ret = ec->get(ino, buf);
    if (ret != OK) return ret;
    if (off < buf.size())
    {
        if (off + len <= buf.size())
        {
            data = std::move(buf.substr(off, len));
        }
        else
        {
            data = std::move(buf.substr(off, buf.size() - off));
        }
    }
    return OK;
}

yfs_client::status
yfs_client::write(inum ino, std::size_t off, std::size_t len, const char *data)
{
    if (!isfile(ino)) return NOENT;
    raii_wrapper rw(lc, ino);
    std::string buf;
    auto ret = ec->get(ino, buf);
    if (ret != OK) return ret;
    buf.resize(std::max(buf.size(), off + len), '\0');
    std::size_t total_len = strlen(data);
    for (unsigned int i = off, j = 0; j < std::min(total_len, len); i++, j++)
    {
        buf[i] = data[j];
    }
    return ec->put(ino, buf);
}

yfs_client::status
yfs_client::unlink(inum parent, const char *name)
{
    if (!isdir(parent)) return NOENT;
    raii_wrapper rw(lc, parent);
    std::string buf;
    auto ret = ec->get(parent, buf);
    if (ret != OK) return ret;
    dirent_list dl(buf);
    if (!dl.match(name)) return NOENT;
    auto file_inum = dl.get(name);
    raii_wrapper rw2(lc, file_inum);
    ret = ec->remove(file_inum);
    if (ret != OK) return ret;
    dl.remove(name);
    return ec->put(parent, dl.to_string());
}
