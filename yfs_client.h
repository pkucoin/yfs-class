#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>
#include <fstream>
#include <random>
#include <climits>

class yfs_client {
  extent_client *ec;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };
  
  class dirent_list {
  public:
    dirent_list(std::string buf)
    {
        unsigned int cur = 0;
        while (cur < buf.size())
        {
            int start = ++cur;
            while (cur < buf.size() && isdigit(buf[cur]))
            {
                ++cur;
            }
            unsigned int len_name = n2i(buf.substr(start, cur - start));
            start = ++cur;
            while (cur < buf.size() && isdigit(buf[cur]))
            {
                ++cur;
            }
            unsigned int len_inum = n2i(buf.substr(start, cur - start));
            start = ++cur;

        std::ofstream outf("debug.txt", std::ofstream::app);
        outf << "rrr start:" << start << " len_name:" << len_name << " len_inum:" << len_inum << " begin:" << buf[start] << "," << buf[start + len_name] << std::endl;
        outf.close();
            data[buf.substr(start, len_name)] = n2i(buf.substr(start + len_name, len_inum));
            cur = start + len_name + len_inum;
        }
    }

    bool match(std::string name)
    {
        return data.find(name) != data.end();
    }

    inum get(const char *name)
    {
        if (!match(name))
        {
            return -1;
        }
        return data[name];
    }

    void add(inum id, const char *name)
    {
        data[name] = id;
    }
    
    std::unordered_map<std::string, inum> get_map()
    {
        return data;
    }
    
    std::string to_string()
    {
        std::string ret = "";
        for (const auto& kv : data)
        {
            std::string id = std::to_string(kv.second);
            // format: @name.size@id.size@nameid 
            // eg. name="hehe" id=123 ret="@4@3@hehe123"
            ret += separator + std::to_string(kv.first.size()) 
                + separator + std::to_string(id.size())
                + separator + kv.first + id;
        }
        return ret;
    }
  private:
    std::unordered_map<std::string, inum> data; 
    const std::string separator = "@";
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
  inum rand_inum();
  std::mt19937 generator;
  std::uniform_int_distribution<int> uid;
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);

  yfs_client::status create(inum parent, const char *name, inum &ret_id); 
  yfs_client::status lookup(inum parent, const char *name, inum &ret_id); 
  yfs_client::status readdir(inum parent, std::unordered_map<std::string, inum>& ret_map); 
};

#endif 