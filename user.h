#include <string>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <map>

using namespace std;

#define MAX_USER 30

class User{
public:
    /* basic */
    int id;
    string name;
    int ssock;
    struct sockaddr_in skInfo;
    map<string, string> env;
    User();
    string getInfo(int);
    string getLoginMsg();
    string getLogoutMsg();
    string getNameMsg();
    bool isOnline();

    /* shell variable */
    vector<int> outLinePfd;
    vector<int> successor;
    map<int, vector<int>> mapSuccessor;
    int iLine;
    bool lsFlag;
    bool pureFlag;
    bool sharePipeFlag;
    bool pipeErrFlag;

    /* user pipe */
    vector<int> recvPipeFrom; // map sender id to pipe fd
    int rpfd;
    bool recvFlag;
    bool sendFlag;
    bool recvFail;
    bool sendFail;
    // User* recverPtr;
    bool hasPipeFrom(int);
    //void setRecvInfo(User*);
    //void setSendInfo(User*);
};

User::User(){
    /* basic */
    this->id = -1;
    this->name = "(no name)";
    this->env["PATH"] = "bin:.";

    /* shell variable */
    iLine = -1;
    lsFlag = false;
    pureFlag = false;
    sharePipeFlag = false;
    pipeErrFlag = false;

    /* user pipe */
    for (int i = 0; i < MAX_USER; i++){
        recvPipeFrom.push_back(-1);
    }
    rpfd = -1;
    recvFlag = false;
    sendFlag = false;
    recvFail = false;
    sendFail = false;
    // recverPtr = NULL;
}
string User::getInfo(int curUserId){ // for "who" cmd
    char info[1024];
    sprintf(info, "%d\t%s\t%s:%d", id, name.c_str(), inet_ntoa(skInfo.sin_addr), skInfo.sin_port);
    string strInfo(info);
    if (curUserId == id){
        strInfo.append("\t<-me");
    }
    return strInfo;
}
string User::getLoginMsg(){
    char msg[1024];
    sprintf(msg, "*** User '%s' entered from %s:%d. ***\n", name.c_str(), inet_ntoa(skInfo.sin_addr), skInfo.sin_port);
    return string(msg);
}
string User::getLogoutMsg(){
    char msg[1024];
    sprintf(msg, "*** User '%s' left. ***\n", name.c_str());
    return string(msg);
}
string User::getNameMsg(){
    char msg[1024];
    sprintf(msg, "*** User from %s:%d is named '%s'. ***\n", inet_ntoa(skInfo.sin_addr), skInfo.sin_port, name.c_str());
    return string(msg);
}
bool User::isOnline(){
    return !(id == -1);
}
bool User::hasPipeFrom(int senderID){
    return !(recvPipeFrom[senderID-1] == -1);
}
/*
void User::setRecvInfo(User* sender){
    recvFlag = true;
    rpfd = recvPipeFrom[sender->id-1];
    recvPipeFrom[sender->id-1] = -1; // reset
}
void User::setSendInfo(User* recver){
    sendFlag = true;
    // recverPtr = recver;
}
*/
