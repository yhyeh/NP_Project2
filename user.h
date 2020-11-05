#include <string>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <map>
using namespace std;

class User{
public:
    int id;
    string name;
    int sock;
    struct sockaddr_in skInfo;
    map<string, string> env;
    User();
    string getInfo(int);
};

User::User(){
    this->id = -1;
    this->name = "(no name)";
    this->env["PATH"] = "bin:.";
}
string User::getInfo(int curUser){
    char info[1024];
    sprintf(info, "%d\t%s\t%s:%d", id, name, inet_ntoa(skInfo.sin_addr), skInfo.sin_port);
    string strInfo(info);
    if (curUser == id){
        strInfo.append("\t<-me");
    }
    return strInfo;
}
/*
// get min unused id
vector<bool> uidMap; // get min unused id
int getMinID(uidMap);

>> new user
assign id
set env

clear env

*/