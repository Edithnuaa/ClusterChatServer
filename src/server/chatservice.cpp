#include "chatservice.hpp"
#include "public.hpp"

#include <string>
#include <muduo/base/Logging.h>
#include <vector>
using namespace std;
using namespace muduo;

ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 登陆业务 ORM框架 业务层操作的都是对象 DAO数据层 有数据库的操作
void ChatService::login(const TcpConnectionPtr& conn, json &js, Timestamp time)
{
    // LOG_INFO << "do login service!!!";
    int id = js["id"];
    string pwd = js["password"];

    User user = _usermodel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 用户已经登陆，不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "该账号已登录，请重新输入新账号";
            conn->send(response.dump());
        }
        else
        {
            // 登陆成功，记录用户连接信息
            {
                // 保证是线程安全的
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            _redis.subscribe(id);

            // 登陆成功, 更新用户状态信息
            user.setState("online");
            _usermodel.updateState(user);
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            //查询用户是否有离线消息
            vector<string> vec = _offlinemsgmodel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取用户的离线消息后，把用户的所有离线消息删除掉
                _offlinemsgmodel.remove(id);
            }

            //查询用户的好友信息并返回
            vector<User> uservec = _friendModel.query(id);
            if (!uservec.empty())
            {
                vector<string> vec2;
                for (User& user : uservec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            vector<Group> usergroups = _groupmodel.queryGroups(id);
            if (!usergroups.empty())
            {
                vector<string> vec1;
                for (Group& gp : usergroups)
                {
                    json js;
                    js["id"] = gp.getId();
                    js["groupname"] = gp.getName();
                    js["groupdesc"] = gp.getDesc();
                    vector<GroupUser> gpusers = gp.getUsers();
                    vector<string> vec2;
                    for (GroupUser& gpu : gpusers)
                    {
                        json jsu;
                        jsu["id"] = gpu.getId();
                        jsu["name"] = gpu.getName();
                        jsu["state"] = gpu.getState();
                        jsu["role"] = gpu.getRole();
                        vec2.push_back(jsu.dump());
                    }
                    js["users"] = vec2;
                    vec1.push_back(js.dump());
                }
                response["groups"] = vec1;
            }

            conn->send(response.dump());
        }
    }
    else
    {
        // 登陆失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或密码错误";
        conn->send(response.dump());
    }

}


// 注册业务
void ChatService::reg(const TcpConnectionPtr& conn, json &js, Timestamp time)
{
    // LOG_INFO << "do reg service!!!";
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _usermodel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}

// 添加好友 msgid id friendis
void ChatService::addFriend(const TcpConnectionPtr& conn, json &js, Timestamp time)
{
    int userid = js["id"];
    int friendid = js["friendid"];

    _friendModel.insert(userid, friendid);
}

// 创建群组
void ChatService::createGroup(const TcpConnectionPtr& conn, json &js, Timestamp time)
{
    int userid = js["id"];
    string name = js["groupname"];
    string desc = js["groupdesc"];

    Group group(-1, name, desc);
    if (_groupmodel.createGroup(group))
    {
        _groupmodel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组
void ChatService::addGroup(const TcpConnectionPtr& conn, json &js, Timestamp time)
{
    int userid = js["id"];
    int groupid = js["groupid"];
    _groupmodel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr& conn, json &js, Timestamp time)
{
    int userid = js["id"];
    int groupid = js["groupid"];
    vector<int> useridvec = _groupmodel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridvec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            it->second->send(js.dump());
        }
        else
        {
            User user = _usermodel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
            _offlinemsgmodel.insert(id, js.dump());
            }
        }
    }
}

MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // LOG_ERROR << "msgid: " << msgid << " can not find handler!";
        return [=](const TcpConnectionPtr& a, json& b, Timestamp c) {
            LOG_ERROR << "msgid: " << msgid << " can not find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

void ChatService::oneChat(const TcpConnectionPtr& conn, json &js, Timestamp time)
{
    int toid = js["to"];
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线, 转发消息, 服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;
        }
    }

    User user = _usermodel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    // toid不存在，存储离线消息
    _offlinemsgmodel.insert(toid, js.dump());
}

// 注册消息以及对应的回调操作
ChatService::ChatService()
{
    _msgHandlerMap.insert({LOGIN_MSG, bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, bind(&ChatService::addFriend, this, _1, _2, _3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG, bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, bind(&ChatService::addFriend, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, bind(&ChatService::groupChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGOUT_MSG, bind(&ChatService::logout, this, _1, _2, _3)});

    if (_redis.connect())
    {
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    _offlinemsgmodel.insert(userid, msg);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr& conn)
{
    User user;

    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it=_userConnMap.begin(); it!=_userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                // 从map表删除用户的连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    _redis.unsubscribe(user.getId());

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _usermodel.updateState(user);
    }
    
    return;
}

void ChatService::logout(const TcpConnectionPtr& conn, json &js, Timestamp time)
{
    int userid = js["id"];
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it!=_userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    _redis.unsubscribe(userid);

    User user;
    user.setId(userid);
    user.setState("offline");
    _usermodel.updateState(user);
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置为offline
    _usermodel.resetState();
}