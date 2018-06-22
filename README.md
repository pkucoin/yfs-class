# 6.824-2012 代码思路、实现及相关话题讨论
## 概述
选择6.824-2012来进行分布式系统的初步学习，主要就是看中其高质量的配套lab可以让人在实践中更好理解分布式系统的相关概念。之所以选择2012年而非2017年的课程lab，主要是考虑到
- 2012年的lab可以看做是一个较完整的mini分布式系统，在给出的代码框架上从底到顶实现了rpc语义、客户端服务器逻辑、缓存及一致性、基于Paxos协议的复制状态机。而2017年的lab则是4个较为独立的topic。
- 2012年的lab使用C++而2017年的lab使用Go。对于C++ 11中引入的concurrency相关的特性一直想找机会学习一下，这一点2012年的lab刚好可以满足。

对于2017课程中，在2012年未涉及的一些重要topic例如Raft协议、工业界的大量新系统研究等，则单独进行学习。

![YFS分布式文件系统结构](https://pdos.csail.mit.edu/archive/6.824-2012/labs/yfs.jpg "YFS分布式文件系统结构")

上图是本lab实现的名为YFS的分布式文件系统的结构图。YFS利用Linux FUSE接口在不修改内核代码的前提下创建了一个文件系统，接管了基础文件操作例如create, mkdir, lookup, read/write等。YFS客户端将这些操作转发到存储服务器extent_server进行实际的读写查询，期间使用锁服务器来保证内容的一致性。客户端和服务器均使用了缓存机制来提高运行效率。锁服务器使用了基于Paxos的复制状态机技术进行了复制来应对宕机。 


## Lab1: 锁服务、RPC语义及相关C++知识
### 目标
Lab1给出了一个rpc基础库和locking service的C/S端的基础框架，需要实现的是rpc的at most once语义（testcase不要求考虑重启宕机）以及locking service的服务器端代码。完成后，locking service的C/S端之间使用rpc进行通信，完成锁的获取acquire和释放release。
### RPC基础库
rpc可以简单理解为通信双方按照约定的协议和数据格式进行信息传递的过程。
著名的rpc库，例如grpc，因为其设计的出发点是一个general rpc framework，需要支持跨平台/多语言等，所以往往是大而全的，阅读起来并不简单。而lab中提供的rpc基础库可谓麻雀虽小五脏俱全，实现上也颇有技巧，值得一读（并不太清楚lab中提供的rpc是完全为此lab编写还是来自开源项目）。其中各部分的主要内容如下:

- fifo 实现了一个blocking queue供PollMgr使用。实现方式是典型的双向链表配合1个互斥锁+2个条件变量的方式：入队和出队分别wait一个条件变量，在完成入队或出队操作后singal对方的条件变量。
- jsl_log 日志模块。定义了一个简单的带级别设置的log宏。如果从一个服务器端通用的logging模块来说显然是远远不够的，而为一个不大的项目引入如log4cpp这样庞大的库也不够简洁。常见的简洁方案有：chenshuo在[muduo](https://github.com/chenshuo/muduo)中使用的Double Buffering（双缓冲），yedf在[C++ 多线程安全无锁日志系统](https://zhuanlan.zhihu.com/p/21477468)中使用O_APPEND方式打开日志文件，dup2系统调用实现轮替。两位都总结了服务器端日志系统应该做到的几点：
  - 高效：写日志本身不应该占用太多系统资源，并且不能阻塞服务器逻辑
  - 线程安全：多个线程能够同时写日志，日志之间不会出现交织
  - 滚动/轮替：在长时间运行的系统中，日志应该可以按照一定规则进行归档，以避免单个日志文件过大
  - 异常处理：程序崩溃退出时，最后的日志不能丢
- marshall 编解码
- connection 封装了socket通信的相关操作
- pollmgr 在connection的基础上封装了io复用（select和epoll）
- slock 作用域锁，等同于C++ 17中的scoped_lock




