#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>
#include <mutex>
using namespace std;
using namespace muduo::net;
using namespace muduo;
using json = nlohmann::json;

#include "redis.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"

// 处理消息的事件回调方法类型
using MsgHandler = std::function<void(const TcpConnectionPtr& conn, 
                                      json& js, Timestamp)>;

//聊天服务器业务类
class ChatService
{
public:
    // 单例对象接口函数
    static ChatService* instance();

    // 登陆业务
    void login(const TcpConnectionPtr& conn, json &js, Timestamp time);
    // 注册业务
    void reg(const TcpConnectionPtr& conn, json &js, Timestamp time);

    // 一对一聊天业务
    void oneChat(const TcpConnectionPtr& conn, json &js, Timestamp time);

    // 添加好友业务
    void addFriend(const TcpConnectionPtr& conn, json &js, Timestamp time);

    // 创建群组
    void createGroup(const TcpConnectionPtr& conn, json &js, Timestamp time);

    // 加入群组
    void addGroup(const TcpConnectionPtr& conn, json &js, Timestamp time);

    // 群组聊天
    void groupChat(const TcpConnectionPtr& conn, json &js, Timestamp time);

    // 获取消息对应的处理器
    MsgHandler getHandler(int msgid);

    // 注销函数
    void logout(const TcpConnectionPtr& conn, json &js, Timestamp time);

    // 处理异常退出
    void clientCloseException(const TcpConnectionPtr& conn);

    // 服务器异常，业务重置方法
    void reset();

    void handleRedisSubscribeMessage(int, string);

private:
    // 存储消息id和其对应的业务处理方法
    unordered_map<int, MsgHandler> _msgHandlerMap;
    ChatService();

    // 存储在线用户的通信连接
    unordered_map<int, TcpConnectionPtr> _userConnMap;

    // 定义互斥锁，保证_userConnMap的线程安全
    mutex _connMutex;

    // 数据操作类对象
    UserModel _usermodel;
    OfflineMsgModel _offlinemsgmodel;
    FriendModel _friendModel;
    GroupModel _groupmodel;

    Redis _redis;
};

#endif