#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <map>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "user.h"

using namespace std;
/* macros */
#define QUE_LEN 50

/* functions */
char** vecStrToChar(vector<string>);
bool hasPipe(vector<string>);
vector<vector<string>> splitPipe(vector<string>);
void purePipe(vector<string>, User*);
int strToInt(string);
void childHandler(int);
int npshellSingle(int);
int passiveTCP(int, int);
User* getValidUser(); // return user with min available id
void resetUser(int);
bool nameExist(User*, string);
void listUser(User*);
void sendMsgTo(User*, string);
void broadcast(string);
int pipeFromOther(vector<string> &cmd); // check if <n
int pipeToOther(vector<string> &cmd); // check if >n

/* global vars */
vector<User*> users;
map<int, User*> ssockToUser;

int main(int argc, char* const argv[]) {
  struct sockaddr_in fsin;	/* the from address of a client	*/
	char *service;	/* service name or port number	*/
	int	msock;		/* master sockets	*/
	socklen_t	alen;			/* from-address length		*/
  fd_set rfds;
  fd_set afds;
  int nfds;
  /* const string */
  string welcome = "****************************************\n";
  welcome.append("** Welcome to the information server. **\n");
  welcome.append("****************************************\n");
  string prompt = "% ";
  
  /* handle child exit */  // signal (SIGCHLD, childHandler);
  struct sigaction action;
  action.sa_handler = childHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &action, NULL) < 0){
    cerr << "sigaction: " << strerror(errno) << endl;
  }
  
  /* init users */
  for (int i = 0; i < MAX_USER; i++){
    users.push_back(new User());
  }
	
  switch (argc) {
    case	2:
      service = argv[1];
      break;
    default:
      cerr << "usage: np_simple [port]" << endl;
      return -1;
	}
  
  //service = "7002";

  msock = passiveTCP(atoi(service), QUE_LEN);
  nfds = __FD_SETSIZE; //getdtablesize();
  FD_ZERO(&afds);
  FD_SET(msock, &afds);
  while (1) {
    memcpy(&rfds, &afds, sizeof(rfds));
    if (select(nfds, &rfds, NULL, NULL, NULL) < 0){
      if (errno == EINTR) {
        // cout << "EINTR but ignore" << endl;
        continue;
      }else {
        cerr << "select: " << strerror(errno) << endl;
        return -1;
      }
    }
    if (FD_ISSET(msock, &rfds)){
      alen = sizeof(fsin);
  		int ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
      if (ssock < 0){
        cerr << "acceptfailed: " << strerror(errno) << endl;
        return -1;
      }
      FD_SET(ssock, &afds); // user accpet
      /* add new user */
      User* newUser = getValidUser();
      newUser->ssock = ssock;
      ssockToUser[ssock] = newUser;
      memcpy(&(newUser->skInfo), &fsin, sizeof(fsin));
      cout << "newuser:: " << newUser->getInfo(newUser->id) << endl;
      cout << "current users size:" << users.size() << endl;
      for (int i = 0; i < users.size(); i++){
        cout << i << "\t" << users[i]->id << users[i]->getInfo(0) << endl;
      }
      cout << "=============================" << endl;
      /* welcome info */
      sendMsgTo(newUser, welcome);
      /* broadcast */
      broadcast(newUser->getLoginMsg());
      /* first "% " */
      sendMsgTo(newUser, prompt);
    }
    for(int fd = 0; fd < nfds; fd++){
      if(fd != msock && FD_ISSET(fd, &rfds)){
        /* I/O redirection */
        int stdinCopy = dup(STDIN_FILENO);
        int stdoutCopy = dup(STDOUT_FILENO);
        int stderrCopy = dup(STDERR_FILENO);

        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);

        int onlineFlag = npshellSingle(fd);
        
        if (onlineFlag == 0){
          // user exit
          dup2(stdinCopy, STDIN_FILENO);
          dup2(stdoutCopy, STDOUT_FILENO);
          dup2(stderrCopy, STDERR_FILENO);
          resetUser(fd);
          close(fd);
          FD_CLR(fd, &afds);
        }
        else{
          cout << prompt << flush; // for waiting next cmd
          dup2(stdinCopy, STDIN_FILENO);
          dup2(stdoutCopy, STDOUT_FILENO);
          dup2(stderrCopy, STDERR_FILENO);
        }
        close(stdinCopy);
        close(stdoutCopy);
        close(stderrCopy);
      }
    }
	}
  return 0;
}

int npshellSingle(int ssock) {
  User* usr = ssockToUser[ssock];
  /* setenv according to user */
  map<string, string>::iterator it;
  for (it = usr->env.begin(); it != usr->env.end(); it++){
    if(setenv(it->first.c_str(), it->second.c_str(), 1) == -1){
      cerr << "Error: set env [" << it->first << "] err" << endl;
    }
  }
  
  string wordInCmd;
  string cmdInLine;
  vector<string> cmd;
  char msgBuf[1024];

  /* user pipe */
  int senderID = -1;
  int recverID = -1;

  /* old while */
  wordInCmd.clear();
  cmdInLine.clear();
  cmd.clear();
  usr->lsFlag = false;
  usr->pureFlag = false;
  usr->sharePipeFlag = false;
  usr->pipeErrFlag = false;
  usr->recvFlag = false;
  usr->sendFlag = false;
  usr->recvFail = false;
  usr->sendFail = false;
  usr->rpfd = -1;
  // usr->recverPtr = NULL;

  getline(cin, cmdInLine);
  if (cmdInLine[cmdInLine.size()-1] == '\r'){
    cmdInLine = cmdInLine.substr(0, cmdInLine.size()-1);
  }
  cmdInLine = cmdInLine.substr(cmdInLine.find_first_not_of(" "), cmdInLine.find_last_not_of(" ")+1);
  
  usr->iLine++; // for num pipe later
  usr->outLinePfd.push_back(-1);
  usr->successor.push_back(-1);
  
  // parse one line
  istringstream inCmd(cmdInLine);
  while (getline(inCmd, wordInCmd, ' ')) {
    cmd.push_back(wordInCmd);
  }

  if (cmd.size() == 0){
    usr->iLine--;
    usr->outLinePfd.pop_back();
    usr->successor.pop_back();
    return 1; // context switch to other user
  }
  else if (cmd[0] == "exit"){
    broadcast(usr->getLogoutMsg());
    return 0; // close ssock
  }
  else if (cmd[0] == "printenv"){
    if (cmd.size() == 2){
      if (getenv(cmd[1].c_str()) != NULL){
        cout << getenv(cmd[1].c_str()) << endl;
      }else{
        // cerr << "Error: no such env" << endl;
      }
    }else{
      cerr << "Error: Usage: printenv [env name]." << endl;
    }
  }
  else if (cmd[0] == "setenv"){
    if (cmd.size() == 3){
      setenv(cmd[1].c_str(), cmd[2].c_str(), 1); // overwrite exist env
      usr->env[cmd[1]] = cmd[2]; // update personal env
    }else{
      cerr << "Error: Usage: setenv [env name] [env value]." << endl;
    }
  }
  else if (cmd[0] == "who"){
    if (cmd.size() == 1){
      listUser(usr);
    }else{
      cerr << "Error: Usage: who." << endl;
    }
  }
  else if (cmd[0] == "name"){
    if (cmd.size() == 2){
      if (nameExist(usr, cmd[1])){
        cout << "*** User '" << cmd[1] << "' already exists. ***" << endl;
      }else{
        usr->name = cmd[1];
        /* broadcast */
        broadcast(usr->getNameMsg());
      }
      
    }else{
      cerr << "Error: Usage: name [newname]." << endl;
    }
  }
  else if (cmd[0] == "tell"){
    if (cmd.size() >= 3){
      string strID = cmd[1];
      int uid = atoi(strID.c_str());
      if (uid < 1 || uid > 30){
        cout << "Error: illegal ID (ID=1~30)" << endl;
        return -1;
      }
      User* recver = users[uid-1];
      
      if (recver->isOnline()){
        /* compose msg */
        string wholeMsg = "*** " + usr->name + " told you ***: "; 
        size_t strIDpos = cmdInLine.find(strID);
        string msg = cmdInLine.erase(0, strIDpos+strID.size());
        msg.erase(0, msg.find_first_not_of(" "));
        wholeMsg.append(msg+"\n");
        sendMsgTo(recver, wholeMsg);
      }else{
        cout << "*** Error: user #"<< strID <<" does not exist yet. ***" << endl;
      }
      
    }else{
      cerr << "Error: Usage: tell [user] [message]." << endl;
    }
  }
  else if (cmd[0] == "yell"){
    if (cmd.size() >= 2){
      /* compose msg */
      string wholeMsg = "*** " + usr->name + " yelled ***: "; 
      size_t yellPos = cmdInLine.find("yell");
      string msg = cmdInLine.erase(0, yellPos+4);
      msg.erase(0, msg.find_first_not_of(" "));
      wholeMsg.append(msg+"\n");

      broadcast(wholeMsg);
    }else{
      cerr << "Error: Usage: yell [message]." << endl;
    }
  }
  else{ // non-buildin function
    
    /* get pipe from other */
    if ((senderID = pipeFromOther(cmd)) > 0){
      usr->recvFlag = true;
      if (1 <= senderID && senderID <= MAX_USER){
        User* sender = users[senderID-1];
        if (sender->isOnline()){
          if (usr->hasPipeFrom(senderID)){
            sprintf(msgBuf, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", usr->name.c_str(), usr->id, sender->name.c_str(), sender->id, cmdInLine.c_str());
            broadcast(string(msgBuf));
            usr->rpfd = usr->recvPipeFrom[sender->id-1];
            usr->recvPipeFrom[sender->id-1] = -1; // reset
          }else {
            cout << "*** Error: the pipe #" << senderID << "->#" << usr->id << " does not exist yet. ***" << endl;
            usr->recvFail = true;
          }
        }else {
          cout << "*** Error: user #" << senderID << " does not exist yet. ***" << endl;
          usr->recvFail = true;
        }
      }else {
          cout << "*** Error: user #" << senderID << " does not exist yet. ***" << endl;
          usr->recvFail = true;
      }
      
    }
    /* pipe output */
    string pipeMark = cmd[cmd.size()-1].substr(0, 1);
    if (pipeMark == "|" || pipeMark == "!"){ // |n or !n
      // cout << "This is |n" << endl;
      string afterMark = cmd[cmd.size()-1].substr(1);
      usr->successor[usr->iLine] = usr->iLine + strToInt(afterMark);
      cmd.pop_back();
      map<int, vector<int>>::iterator mit;
      mit = usr->mapSuccessor.find(usr->successor[usr->iLine]);
      if (mit == usr->mapSuccessor.end()){ // pass to new successor
        usr->mapSuccessor[usr->successor[usr->iLine]] = vector<int>(2, -1);
      }else {
        usr->sharePipeFlag = true;
      }
      if (pipeMark == "!") {
        usr->pipeErrFlag = true;
        // cout << "This is !n" << endl;
      }
      purePipe(cmd, usr);
      
      if (!usr->sharePipeFlag){
        usr->mapSuccessor[usr->successor[usr->iLine]][0] = usr->outLinePfd[usr->iLine];
      }else {
        close(usr->outLinePfd[usr->iLine]);
        // cout << "parent close [" << outLinePfd[iLine] << "]" << endl;
      }
    }
    else if ((recverID = pipeToOther(cmd)) > 0){ // >n : send to other user
      usr->sendFlag = true;
      if (1 <= recverID && recverID <= MAX_USER){
        User* recver = users[recverID-1];
        if (recver->isOnline()){
          if (recver->hasPipeFrom(usr->id)){
            cout << "*** Error: the pipe #" << usr->id << "->#" << recverID << " already exists. ***" << endl;
            usr->sendFail = true;
          }else {
            sprintf(msgBuf, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", usr->name.c_str(), usr->id, cmdInLine.c_str(), recver->name.c_str(), recver->id);
            broadcast(string(msgBuf));
            purePipe(cmd, usr);
            recver->recvPipeFrom[usr->id-1] = usr->outLinePfd[usr->iLine];
          }
        }else {
          cout << "*** Error: user #" << recverID << " does not exist yet. ***" << endl;
          usr->sendFail = true;
        }
      }else {
        cout << "*** Error: user #" << recverID << " does not exist yet. ***" << endl;
        usr->sendFail = true;
      }

      if(usr->sendFail){
        purePipe(cmd, usr);
        char buf[256];
        ssize_t outSize;
        ofstream redirectFile("/dev/null");
        while(outSize = read(usr->outLinePfd[usr->iLine], buf, sizeof(buf)-1)){
          // cout << "buf catch size: " << outSize << endl;
          buf[outSize] = '\0';
          string strBuf(buf);
          redirectFile << strBuf;
          memset(buf, 0, sizeof(buf));
        }

        close(usr->outLinePfd[usr->iLine]);
        redirectFile.close();
      }
      
      
    }
    else if (cmd.size() > 1 && cmd[cmd.size()-2] == ">"){
      // cout << "This is >" << endl;
      string fname = cmd.back();
      // remove string after >
      cmd.pop_back();
      cmd.pop_back();
      // process as purepipe
      purePipe(cmd, usr);
      char buf[256];
      ssize_t outSize;
      ofstream redirectFile(fname);
      while(outSize = read(usr->outLinePfd[usr->iLine], buf, sizeof(buf)-1)){
        // cout << "buf catch size: " << outSize << endl;
        buf[outSize] = '\0';
        string strBuf(buf);
        redirectFile << strBuf;
        memset(buf, 0, sizeof(buf));
      }

      close(usr->outLinePfd[usr->iLine]);
      redirectFile.close();
      
    }
    else { // 0 ~ n pipe 
      // cout << "This is pure pipe" << endl;
      usr->pureFlag = true;
      purePipe(cmd, usr);
      if (usr->lsFlag == true) return 1;
      char buf[256];
      ssize_t outSize;
      while(outSize = read(usr->outLinePfd[usr->iLine], buf, sizeof(buf)-1)){
        // cout << "buf catch size: " << outSize << endl;
        buf[outSize] = '\0';
        string strBuf(buf);
        cout << strBuf;
        memset(buf, 0, sizeof(buf));
      }
      close(usr->outLinePfd[usr->iLine]);
    }
    
  }

  return 1; // context switch to other user
}

void purePipe(vector<string> cmd, User* usr){ // fork and connect sereval worker, but not guarantee finish 
  vector<vector<string>> cmdVec = splitPipe(cmd);
  // childPids.clear();
  pid_t pid;
  int pfd[2];
  int prevPipeOutput = -1;
  
  /* pure ls */
  if (usr->pureFlag && cmdVec.size() == 1 && cmdVec[0][0] == "ls") { 
    usr->lsFlag = true;
    if ((pid = fork()) < 0) {
      cerr << "Error: fork failed" << endl;
      exit(0);
    }
    if (pid == 0){ // child
      if (execvp(cmdVec[0][0].c_str(), vecStrToChar(cmdVec[0])) == -1){
        cerr << "Unknown command: [" << cmdVec[0][0] << "]." << endl;
        //cerr << "Unknown command: [" << cmdVec[0][0] << "]." << strerror(errno) << endl;
        exit(0);
      } 
    }else{ // parent
      waitpid(pid, NULL, 0);
    }
    return;
  }

  // 0 ~ n pipe
  for (int icmd = 0; icmd < cmdVec.size(); icmd++){
    vector<string> curCmd = cmdVec[icmd];
    if (pipe(pfd) == -1){
      cerr << "Error: pipe create fail" << endl;
      exit(0);
    }
    while ((pid = fork()) < 0) {
      waitpid(-1, NULL, 0);
      // cerr << "Error: fork failed" << endl;
      //exit(0);
    }
    /* child process */
    if (pid == 0){
      
      if (icmd == 0){ // first cmd
        /* recv pipe from other */
        if (usr->recvFlag){
          if (usr->recvFail){
            int devNull = open("/dev/null", O_RDWR);
            dup2(devNull, STDIN_FILENO); // stdin from previous cmd
            close(prevPipeOutput);
          }else{
            prevPipeOutput = usr->rpfd;
            dup2(prevPipeOutput, STDIN_FILENO); // stdin from previous cmd
            close(prevPipeOutput);
          }
        }
        /* has number pipe predecessor */
        map<int, vector<int>>::iterator mit;
        mit = usr->mapSuccessor.find(usr->iLine);
        if (mit != usr->mapSuccessor.end()){
          prevPipeOutput = usr->mapSuccessor[usr->iLine][0];
          dup2(prevPipeOutput, STDIN_FILENO); // stdin from previous cmd
          close(prevPipeOutput);
          close(usr->mapSuccessor[usr->iLine][1]);
        }
        if(usr->successor[usr->iLine] != -1 && icmd == cmdVec.size()-1){
          // numpipe && last cmd
          if (usr->sharePipeFlag){
            dup2(usr->mapSuccessor[usr->successor[usr->iLine]][1], STDOUT_FILENO); // output to shared pipe
            if (usr->pipeErrFlag){
              int tgCopy = dup(usr->mapSuccessor[usr->successor[usr->iLine]][1]);
              dup2(tgCopy, STDERR_FILENO); // output to shared pipe
              close(tgCopy);
            }
            close(usr->mapSuccessor[usr->successor[usr->iLine]][1]);
          }else {
            dup2(pfd[1], STDOUT_FILENO); // output to pipe
            if (usr->pipeErrFlag){
              int tgCopy = dup(pfd[1]);
              dup2(tgCopy, STDERR_FILENO); // output to pipe
              close(tgCopy);
            }
          }
        }else {
          dup2(pfd[1], STDOUT_FILENO); // output to pipe
        }
        
      }else { // mid cmd
        dup2(prevPipeOutput, STDIN_FILENO); // stdin from previous cmd
        close(prevPipeOutput);
        if (usr->successor[usr->iLine] != -1 && icmd == cmdVec.size()-1){
          // |n or !n & last cmd
          if (usr->sharePipeFlag){
            dup2(usr->mapSuccessor[usr->successor[usr->iLine]][1], STDOUT_FILENO); // output to shared pipe
            if (usr->pipeErrFlag){
              int tgCopy = dup(usr->mapSuccessor[usr->successor[usr->iLine]][1]);
              dup2(tgCopy, STDERR_FILENO); // output to shared pipe
              close(tgCopy);
            }
            close(usr->mapSuccessor[usr->successor[usr->iLine]][1]);
          }else {
            dup2(pfd[1], STDOUT_FILENO); // output to pipe
            if (usr->pipeErrFlag){
              int tgCopy = dup(pfd[1]);
              dup2(tgCopy, STDERR_FILENO); // output to pipe
              close(tgCopy);
            }
          }
        }else {
          dup2(pfd[1], STDOUT_FILENO); // output to pipe
        }
      }
      int nfds = __FD_SETSIZE; // getdtablesize();
      for (int fd = 3; fd <= nfds; fd++){
        if (fd != prevPipeOutput){
          close(fd);
        }
      }
      close(pfd[1]);

      if(execvp(curCmd[0].c_str(), vecStrToChar(curCmd)) == -1){
        cerr << "Unknown command: [" << curCmd[0] << "]." << endl;
        //cerr << "Unknown command: [" << curCmd[0] << "]." << strerror(errno) << endl;
        close(pfd[1]); // necessary?
        exit(0);
      }
    } /* parent process */
    else {
      // cout << "create pfd: " << pfd[0] << " " << pfd[1] << endl; 
      if (usr->successor[usr->iLine] != -1 && !usr->sharePipeFlag && icmd == cmdVec.size()-1){
        // numpipe && pass to new successor && last cmd
        usr->mapSuccessor[usr->successor[usr->iLine]][1] = pfd[1];
      }else {
        close(pfd[1]);
        // cout << "parent close [" << pfd[1] << "]" << endl;
      }
      // childPids.push_back(pid);
      if (icmd != 0){
        close(prevPipeOutput);
      }
      prevPipeOutput = pfd[0];
    }
  }
  usr->outLinePfd[usr->iLine] = pfd[0];
  map<int, vector<int>>::iterator mit;
  mit = usr->mapSuccessor.find(usr->iLine);
  if (mit != usr->mapSuccessor.end()){ // is number pipe receiver
    close(usr->mapSuccessor[usr->iLine][0]);
    close(usr->mapSuccessor[usr->iLine][1]);
    // cout << "parent close [" << mapSuccessor[iLine][0] << "]" << endl;
    // cout << "parent close [" << mapSuccessor[iLine][1] << "]" << endl;
  }
  if(usr->recvFlag && !usr->recvFail){
    close(usr->rpfd);
  }
}

int pipeFromOther(vector<string> &cmd){ // check if <n
/* return <n idx in cmd, -1 if not found */
  for (int i = 1; i < cmd.size(); i++){
    if (cmd[i].size() <= 1) continue; // single char <, |
    int senderID = atoi(cmd[i].substr(1).c_str());
    if (cmd[i].substr(0, 1) == "<" && senderID > 0){
      cmd.erase(cmd.begin() + i);
      return senderID;
    }
  }
  return -1;
}

int pipeToOther(vector<string> &cmd){ // check if >n
  /* return >n idx in cmd, -1 if not found */
  for (int i = 1; i < cmd.size(); i++){
    if (cmd[i].size() <= 1) continue; // single char >, |
    int recverID = atoi(cmd[i].substr(1).c_str());
    if (cmd[i].substr(0, 1) == ">" && recverID > 0){
      cmd.erase(cmd.begin() + i);
      return recverID;
    }
  }
  return -1;
}

void sendMsgTo(User* usr, string msg){
  int stdinCopy = dup(STDIN_FILENO);
  int stdoutCopy = dup(STDOUT_FILENO);
  int stderrCopy = dup(STDERR_FILENO);

  dup2(usr->ssock, STDIN_FILENO);
  dup2(usr->ssock, STDOUT_FILENO);
  dup2(usr->ssock, STDERR_FILENO);

  cout << msg << flush; // for waiting next cmd

  dup2(stdinCopy, STDIN_FILENO);
  dup2(stdoutCopy, STDOUT_FILENO);
  dup2(stderrCopy, STDERR_FILENO);
  close(stdinCopy);
  close(stdoutCopy);
  close(stderrCopy);
}

void broadcast(string msg){
  for (int u = 0; u < MAX_USER; u++){
    if (users[u]->isOnline()){
      sendMsgTo(users[u], msg);
    }
  }
}

void resetUser(int ssock){
  User* exitUser = ssockToUser[ssock];
  delete exitUser; //clear old info
  exitUser = new User(); // reset 
  ssockToUser.erase(ssock);
}

User* getValidUser(){
  for (int u = 0; u < MAX_USER; u++){
    if (users[u]->id == -1){
      users[u]->id = u + 1;
      return users[u];
    }
  }
  return NULL; // users full
}

void listUser(User* curUsr){
  string header = "<ID>\t<nickname>\t<IP:port>\t<indicate me>";
  cout << header << endl;
  for (int u = 0; u < MAX_USER; u++){
    if (users[u]->id == -1) continue;
    cout << users[u]->getInfo(curUsr->id) << endl;
  }
}

bool nameExist(User* curUsr, string newName){
  if (newName == "(no name)") return false;

  for (int u = 0; u < MAX_USER; u++){
    if (users[u]->id == curUsr->id) continue;
    if (users[u]->name == newName){
      return true;
    }
  }
  return false;
}

int passiveTCP(int service, int queLen){
  struct protoent *ppe;
  struct sockaddr_in sin;
  int sock;
  int sockType = SOCK_STREAM;
  string protocol = "tcp";

  memset((char*)&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons(service);
  /* Map protocol name to protocol number */
	if ((ppe = getprotobyname(protocol.c_str())) == 0){
		cerr << "can't get \"" << protocol << "\" protocol entry\n" << endl;
    return -1;
  }
  /* Allocate a socket */
	sock = socket(PF_INET, sockType, ppe->p_proto);
	if (sock < 0){
		cerr << "can't create socket: " << strerror(errno) << endl;
    return -1;
  }
  /* Bind the socket */
	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0){
		cerr << "can't bind to " << service << " port: " << strerror(errno) << endl;
    return -1;
  }
	if (listen(sock, queLen) < 0){
		cerr << "can't listen on " << service << " port: " << strerror(errno) << endl;
    return -1;
  }
	return sock;
}

char** vecStrToChar(vector<string> cmd){
  char** result = (char**)malloc(sizeof(char*)*(cmd.size()+1));
  for(int i = 0; i < cmd.size(); i++){
    result[i] = strdup(cmd[i].c_str());
  }
  result[cmd.size()] = NULL;
  return result;
}

bool hasPipe(vector<string> cmd){
  bool flag = false;
  for (int i = 0; i < cmd.size(); i++){
    if (cmd[i] == "|"){
      flag = true;
      break;
    }
  }
  return flag;
}

vector<vector<string>> splitPipe(vector<string> cmd){
  vector<vector<string>> cmdVec;
  vector<string>::iterator ibeg = cmd.begin();
  for (vector<string>::iterator icur = cmd.begin(); icur != cmd.end(); icur++){
    if (*icur == "|"){
      vector<string> beforePipe(ibeg, icur);
      //printStrVec(beforePipe);
      cmdVec.push_back(beforePipe);
      ibeg = icur + 1;
    }else if (icur == cmd.end()-1){
      vector<string> afterPipe(ibeg, cmd.end());
      //printStrVec(afterPipe);
      cmdVec.push_back(afterPipe);
    }
  }
  return cmdVec;
}

int strToInt(string str){
  return atoi(str.c_str());
}

void childHandler(int sig){
  // cout << "child finished" << endl;
  while(waitpid(-1, NULL, WNOHANG) > 0){

  }
}
