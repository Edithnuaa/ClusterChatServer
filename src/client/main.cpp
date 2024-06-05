#include <iostream>
#include <thread>
#include <ctime>
#include <vector>
#include <string>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

#include "nlohmann/json.hpp"
#include "group.hpp"
#include "user.hpp"
#include "public.hpp"
using json = nlohmann::json;

// 记录当前系统登陆的用户信息
User g_currentUser;
// 记录当前用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前用户的群组列表信息
vector<Group> g_currentUserGroupList;
// 显示当前登陆成功用户的基本信息
void showCurrentUserData();
// 控制聊天页面程序
bool isMainMenuRunning = false;

// 接受线程
void readTaskHandler(int clientfd);
// 获取系统时间
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int clientfd);

// 客户端程序实现，mian线程作为发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if (argc<3)
    {
        cerr << "command invalid example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析命令行参数传递给ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要链接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    if (connect(clientfd, (sockaddr*)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    for (;;)
    {
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice: ";
        int choice = 0;
        cin >> choice;
        cin.get(); // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // 登陆业务
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid: ";
            cin >> id;
            cin.get(); // 读掉缓冲区残留的回车
            cout << "userpassword: ";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(clientfd, request.c_str(), strlen(request.c_str())+1, 0);
            if (len == -1)
            {
                cerr << "send login msg error: " << request << endl;
            }
            else
            {
                char buffer[1024] = {0};
                len = recv(clientfd, buffer, 1024, 0);
                if (len == -1) 
                {
                    cerr << "recv login response error" << endl;
                }
                else
                {
                    json responsejs = json::parse(buffer);
                    if (responsejs["errno"] != 0) 
                    {
                        cerr << responsejs["errmsg"] << endl;
                    }
                    else
                    {
                        g_currentUser.setId(responsejs["id"]);
                        g_currentUser.setName(responsejs["name"]);

                        if (responsejs.contains("friends"))
                        {
                            g_currentUserFriendList.clear();
                            vector<string> vec = responsejs["friends"];
                            for (string &str :vec)
                            {
                                json js = json::parse(str);
                                User user;
                                user.setId(js["id"]);
                                user.setName(js["name"]);
                                user.setState(js["state"]);
                                g_currentUserFriendList.push_back(user);
                            }
                        }

                        if (responsejs.contains("groups"))
                        {
                            vector<string> vec1 = responsejs["groups"];
                            g_currentUserGroupList.clear();
                            for (string &groupstr : vec1)
                            {
                                json grpjs = json::parse(groupstr);
                                Group group;
                                group.setId(grpjs["id"]);
                                group.setName(grpjs["groupname"]);
                                group.setDesc(grpjs["groupdesc"]);

                                vector<string> vec2 = grpjs["users"];
                                for (string &userstr : vec2)
                                {
                                    GroupUser user;
                                    json js = json::parse(userstr);
                                    user.setId(js["id"]);
                                    user.setName(js["name"]);
                                    user.setState(js["state"]);
                                    user.setRole(js["role"]);
                                    group.getUsers().push_back(user);
                                }

                                g_currentUserGroupList.push_back(group);
                            }
                        }
                        showCurrentUserData();

                        if (responsejs.contains("offlinemsg"))
                        {
                            vector<string> vec = responsejs["offlinemsg"];
                            for (string &str : vec)
                            {
                                json js = json::parse(str);
                                int msgtype = js["msgid"];
                                if (msgtype == ONE_CHAT_MSG)
                                {
                                    cout << js["time"] << " [" << js["id"] << "] " << js["name"]
                                        << " said: " << js["msg"] << endl;
                                    continue;
                                }
                                else if (msgtype == GROUP_CHAT_MSG)
                                {
                                    cout << "GroupMessage[" << js["groupid"] << "] " 
                                        << js["time"] << " [" << js["id"] << "] " << js["name"]
                                        << " said: " << js["message"] << endl;
                                }
                            }
                        }

                        static int threadnumber = 0;
                        if (threadnumber == 0)
                        {
                            std::thread readTask(readTaskHandler, clientfd);
                            readTask.detach();
                            threadnumber++;
                        }

                        isMainMenuRunning = true;
                        mainMenu(clientfd);
                    }
                }
            }
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username: ";
            cin.getline(name, 50);
            cout << "userpassword: ";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(clientfd, request.c_str(), strlen(request.c_str())+1, 0);
            if (len == -1)
            {
                cerr << "send reg msg error:" << request << endl;
            }
            else
            {
                char buffer[1024] = {0};
                len = recv(clientfd, buffer, 1024, 0);
                if (len == -1) // 注册失败
                {
                    cerr << "recv reg response error" << endl;
                }
                else // 注册成功
                {
                    json responsejs = json::parse(buffer);
                    if (responsejs["errno"] != 0)
                    {
                        cerr << name << " is already exist, register error!" << endl;
                    }
                    else
                    {
                        cout << name << " register success, userid is " << responsejs["id"]
                             << ", do not forget it!" << endl;
                    }
                }
            }
        }
        break;
        case 3:
            close(clientfd);
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }
    return 0;
}


void showCurrentUserData()
{
    cout << "===================LOGIN USER===================" << endl;
    cout << "current login user => id: " << g_currentUser.getId() << "name: " << g_currentUser.getName() << endl;
    cout << "------------------Friend List-------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "-----------------Group List---------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState() << " "
                     << user.getRole() << endl;
            }
        }
    }
    cout << "=======================================" << endl;
}

void readTaskHandler(int clientfd)
{
    while (isMainMenuRunning)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);
        if (len == -1 || len == 0)
        {
            close(clientfd);
            exit(-1);
        }
        
        json js = json::parse(buffer);
        int msgtype = js["msgid"];
        if (msgtype == ONE_CHAT_MSG)
        {
            cout << js["time"] << " [" << js["id"] << "] " << js["name"]
                 << " said: " << js["msg"] << endl;
            continue;
        }
        else if (msgtype == GROUP_CHAT_MSG)
        {
            cout << "GroupMessage[" << js["groupid"] << "] " 
                 << js["time"] << " [" << js["id"] << "] " << js["name"]
                 << " said: " << js["message"] << endl;
            continue;
        }
    }
}

string getCurrentTime()
{

    return "TIME";
}

void help(int fd=0, string str="");
void chat(int, string);
void addfriend(int, string);
void creategroup(int, string);
void addgroup(int, string);
void groupchat(int, string);
void quit(int, string str="");

unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令，格式help"},
    {"chat", "一对一聊天，格式chat:friendid:message"},
    {"addfriend", "添加好友，格式addfriend:friendid"},
    {"creategroup", "创建群组，格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式addgroup:groupid"},
    {"groupchat", "群聊，格式groupchat:groupid:message"},
    {"quit", "注销，格式quit"},
};

unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"quit", quit},
};

// 主聊天业务程序
void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command;
        int idx = commandbuf.find(":");
        if (idx == -1)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        it->second(clientfd, commandbuf.substr(idx+1, commandbuf.size()-idx));
    }
}

void help(int, string)
{
    cout << "show command list >>> " << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}

void addfriend(int clientfd, string str)
{
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str())+1, 0);
    if (len == -1)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

void chat(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }
    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx+1, str.size()-idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["to"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str())+1, 0);
    if (len == -1)
    {
        cerr << "send chat msg error -> " << buffer << endl;
    }
}

void creategroup(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }
    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx+1, str.size()-idx);
    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()), 0);
    if (len == -1)
    {
        cerr << "send creategroup msg error!" << endl;
        return;
    }
    return;
}

void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()), 0);
    if (len == -1)
    {
        cerr << "send addgroup msg error!" << endl;
        return;
    }
    return;
}

void groupchat(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "groupchar command invalid!" << endl;
        return;
    }
    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx+1, str.size()-idx);
    cout << message << endl;
    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["message"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()), 0);
    if (len == -1)
    {
        cerr << "send groupchat msg error!" << endl;
        return;
    }
    return;
}

void quit(int clientfd, string)
{
    json js;
    js["msgid"] = LOGOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()), 0);
    if (len == -1)
    {
        cerr << "quit error!" << endl;
        return;
    }
    else
    {
        isMainMenuRunning = false;
    }
}