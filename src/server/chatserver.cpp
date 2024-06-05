#include "chatserver.hpp"
#include "chatservice.hpp"

#include <nlohmann/json.hpp>
#include <functional>
#include <string>
using namespace std;
using namespace placeholders;
using json =  nlohmann::json;

ChatServer::ChatServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       const string& name)
    : _server(loop, listenAddr, name), _loop(loop)
{
    _server.setConnectionCallback(bind(&ChatServer::onConnection, this, _1));

    _server.setMessageCallback(bind(&ChatServer::onMessage, this, _1, _2, _3));

    _server.setThreadNum(4);
}

void ChatServer::start() {
    _server.start();
}

void ChatServer::onConnection(const TcpConnectionPtr& conn) {
    // 客户端断开链接
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

void ChatServer::onMessage(const TcpConnectionPtr& conn,
                           Buffer *buffer,
                           Timestamp time) {
    string buf = buffer->retrieveAllAsString();
    // 数据的反序列化
    json js = json::parse(buf);

    // 将网络模块的代码和业务模块的代码解耦
    // 通过js["msgid"]调用某一个回调函数, 获取一个业务handler
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
    
}