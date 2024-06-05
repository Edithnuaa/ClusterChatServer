#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

//User表的数据操作类
class UserModel
{
public:
    // User数据的增加
    bool insert(User &user);

    // 根据用户id查询用户信息
    User query(int id);

    bool updateState(User user);

    // 重置用户的状态信息
    void resetState();


private:


};


#endif