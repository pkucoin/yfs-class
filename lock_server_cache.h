#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>

#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"


class lock_server_cache {
    public:
        class set_queue 
        {
        private:
            std::set<std::string> s;
            std::queue<std::string> q; 
        public:
            void push(std::string cid)
            {
                if (s.find(cid) == s.end())
                {
                    s.insert(cid);
                    q.push(cid);
                }
            }

            void pop()
            {
                auto cid = q.front();
                s.erase(cid);
                q.pop();
            }

            const std::string& front() const 
            {
                return q.front();
            }

            bool empty()
            {
                return s.empty();
            }
        };
        struct server_lock 
        {
            enum XXSTATUS 
            {
                FREE,
                LOCKED,
                REVOKE_SENT,
            };
            int status;
            std::string client_id;
            std::mutex mtx;
            set_queue waiting_cids;
            server_lock(std::string cid) : status(FREE), client_id(cid) {}
        };
        lock_server_cache();
        lock_protocol::status stat(lock_protocol::lockid_t, int &);
        int acquire(lock_protocol::lockid_t, std::string id, int &);
        int release(lock_protocol::lockid_t, std::string id, int &);
    private:
        int nacquire;
        std::mutex server_mtx;
        std::unordered_map<lock_protocol::lockid_t, std::shared_ptr<server_lock>> lock_cache;
};

#endif
