#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
using namespace std;

class Redis
{
public:
    Redis();
    ~Redis();

    bool connect();

    bool publish(int channel, string message);

    bool subscribe(int channel);

    bool unsubscribe(int channel);

    void observer_channel_message();

    // 初始化向业务层上报通道消息的回调对象
    void init_notify_handler(function<void(int,string)> fn);

private:
    // redis上下文，负责publish
    redisContext *_publish_context;
    // redis上下文，负责订阅消息
    redisContext *_subscribe_context;
    // 回调操作，收到订阅的消息，给service层上报
    function<void(int, string)> _notify_message_handler;

};

#endif