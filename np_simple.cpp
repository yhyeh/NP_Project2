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

using namespace std;

/* functions */
char** vecStrToChar(vector<string>);
bool hasPipe(vector<string>);
vector<vector<string>> splitPipe(vector<string>);
void purePipe(vector<string>);
int strToInt(string);
// vecetor<int> getPredecessor(int);
void childHandler(int);
int npshell();
int passiveTCP(int, int);

/* global vars */
int redirectFd;
// vector<string> lineHistory;
vector<int> outLinePfd;
vector<int> successor;
int iLine = -1;
vector<pid_t> childPids;
bool lsFlag;
bool pureFlag;
map<int, vector<int>> mapSuccessor;
map<int, vector<int>>::iterator mit;
bool sharePipeFlag;
bool pipeErrFlag;
char outBuf[1024];


/* macros */
#define QUE_LEN 1

int main(int argc, char* const argv[]) {
  struct sockaddr_in fsin;	/* the from address of a client	*/
	char *service;	/* service name or port number	*/
	int	msock, ssock;		/* master & slave sockets	*/
	socklen_t	alen = sizeof(fsin);			/* from-address length		*/
  pid_t pid;
  signal (SIGCHLD, childHandler);
	/*
  switch (argc) {
    case	2:
      service = argv[1];
      break;
    default:
      cerr << "usage: np_simple [port]" << endl;
      return -1;
	}
  */
  service = "7001";

  msock = passiveTCP(atoi(service), QUE_LEN);
  while (1) {
    cout << "Waiting client ... " << endl;
		ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
		
		if (ssock < 0){
	  	cerr << "acceptfailed: " << strerror(errno) << endl;
      return -1;
    }

    cout << "Accept a client from "<< inet_ntoa(fsin.sin_addr) << ":";
    cout << fsin.sin_port << "\n" << endl;

    while ((pid = fork()) < 0) {
      waitpid(-1, NULL, 0);
      // cerr << "Error: fork failed" << endl;
      //exit(0);
    }
    if(pid == 0){ //child
      close(msock);
      int sockCopy1 = dup(ssock);
      int sockCopy2 = dup(ssock);

      dup2(ssock, STDIN_FILENO);
      dup2(sockCopy1, STDOUT_FILENO);
      dup2(sockCopy2, STDERR_FILENO);
      
      npshell();
      
      close(ssock);
      close(sockCopy1);
      close(sockCopy2);
      exit(0);
    }else{ // parent
      close(ssock);
      //waitpid(pid, NULL, 0);
    }
    
	}
  
  return 0;
}

int passiveTCP(int service, int queLen){
  struct protoent *ppe;
  struct sockaddr_in sin;
  int sock;
  int sockType = SOCK_STREAM;
  string protocol = "tcp";
  int enable = 1;
  int disable = 0;

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
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    cerr << "setsockopt(SO_REUSEADDR) failed" << endl;
    return -1;
  }
  /* Bind the socket */
	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0){
		cerr << "can't bind to " << service << "port: " << strerror(errno) << endl;
    return -1;
  }
	if (listen(sock, queLen) < 0){
		cerr << "can't listen on " << service << "port: " << strerror(errno) << endl;
    return -1;
  }
	return sock;
}

int npshell() {

  if(setenv("PATH", "bin:.", 1) == -1){
    cerr << "Error: set env err" << endl;
  }
  //signal (SIGCHLD, childHandler);
  string wordInCmd;
  string cmdInLine;
  vector<string> cmd;
  char inBuf[2048];
  ssize_t inSize;

  while(true){
    wordInCmd.clear();
    cmd.clear();
    lsFlag = false;
    pureFlag = false;
    sharePipeFlag = false;
    pipeErrFlag = false;
    cout << "% ";
    // sprintf(outBuf, "% ");
    // write(sock, outBuf, strlen(outBuf));
    getline(cin, cmdInLine);
    //cout << "cmd: " << cmdInLine << endl;
    if (cmdInLine[cmdInLine.size()-1] == '\r'){
      cmdInLine = cmdInLine.substr(0, cmdInLine.size()-1);
    }
    // lineHistory.push_back(cmdInLine);
    iLine++; // for num pipe later
    outLinePfd.push_back(-1);
    successor.push_back(-1);
    
    // parse one line
    istringstream inCmd(cmdInLine);
    while (getline(inCmd, wordInCmd, ' ')) {
      cmd.push_back(wordInCmd);
    }

    if (cmd.size() == 0){
      iLine--;
      outLinePfd.pop_back();
      successor.pop_back();
      continue;
    }else if (cmd[0] == "exit"){
      //exit(0);
      return 0;
    }else if (cmd[0] == "printenv"){
      if (cmd.size() == 2){
        if (getenv(cmd[1].c_str()) != NULL){
          cout << getenv(cmd[1].c_str()) << endl;
          //sprintf(outBuf, "%s\n", getenv(cmd[1].c_str()));
          //write(sock, outBuf, strlen(outBuf));
        }else{
          // cerr << "Error: no such env" << endl;
        }
      }else{
        cerr << "Error: missing argument" << endl;
      }
    }else if (cmd[0] == "setenv"){
      if (cmd.size() == 3){
        setenv(cmd[1].c_str(), cmd[2].c_str(), 1); // overwrite exist env
      }else{
        cerr << "Error: missing argument" << endl;
      }
    }else{ // non-buildin function
      
      // process each cmd seperate by > or |
      // Where is > or | ?
      string pipeMark = cmd[cmd.size()-1].substr(0, 1);
      if (pipeMark == "|" || pipeMark == "!"){ // |n or !n
        // cout << "This is |n" << endl;
        string afterMark = cmd[cmd.size()-1].substr(1);
        successor[iLine] = iLine + strToInt(afterMark);
        cmd.pop_back();
        mit = mapSuccessor.find(successor[iLine]);
        if (mit == mapSuccessor.end()){ // pass to new successor
          mapSuccessor[successor[iLine]] = vector<int>(2, -1);
        }else {
          sharePipeFlag = true;
        }
        if (pipeMark == "!") {
          pipeErrFlag = true;
          // cout << "This is !n" << endl;
        }
        purePipe(cmd);
        
        if (!sharePipeFlag){
          mapSuccessor[successor[iLine]][0] = outLinePfd[iLine];
        }else {
          close(outLinePfd[iLine]);
          // cout << "parent close [" << outLinePfd[iLine] << "]" << endl;
        }
      }
      else if (cmd.size() > 1 && cmd[cmd.size()-2] == ">"){
        // cout << "This is >" << endl;
        string fname = cmd.back();
        // remove string after >
        cmd.pop_back();
        cmd.pop_back();
        // process as purepipe
        purePipe(cmd);
        char buf[256];
        ssize_t outSize;
        ofstream redirectFile(fname);
        while(outSize = read(outLinePfd[iLine], buf, sizeof(buf)-1)){
          // cout << "buf catch size: " << outSize << endl;
          buf[outSize] = '\0';
          string strBuf(buf);
          redirectFile << strBuf;
          //write(sock, buf, strlen(buf));
          memset(buf, 0, sizeof(buf));
        }

        close(outLinePfd[iLine]);
        redirectFile.close();
        
      }
      else { // 0 ~ n pipe 
        // cout << "This is pure pipe" << endl;
        pureFlag = true;
        purePipe(cmd);
        if (lsFlag == true) continue;
        char buf[256];
        ssize_t outSize;
        while(outSize = read(outLinePfd[iLine], buf, sizeof(buf)-1)){
          // cout << "buf catch size: " << outSize << endl;
          buf[outSize] = '\0';
          string strBuf(buf);
          cout << strBuf;
          //write(sock, buf, strlen(buf));
          memset(buf, 0, sizeof(buf));
        }
        close(outLinePfd[iLine]);
      }
      
    }
  }

  return 0;
}

void purePipe(vector<string> cmd){ // fork and connect sereval worker, but not guarantee finish 
  vector<vector<string>> cmdVec = splitPipe(cmd);
  childPids.clear();
  pid_t pid;
  int pfd[2];
  int prevPipeOutput = -1;
  
  /* pure ls */
  if (pureFlag && cmdVec.size() == 1 && cmdVec[0][0] == "ls") { 
    lsFlag = true;
    if ((pid = fork()) < 0) {
      cerr << "Error: fork failed" << endl;
      exit(0);
    }
    if (pid == 0){ // child
      if (execvp(cmdVec[0][0].c_str(), vecStrToChar(cmdVec[0])) == -1){
        cerr << "Unknown command: [" << cmdVec[0][0] << "]." << strerror(errno) << endl;
        //sprintf(outBuf, "Unknown command: [%s].", cmdVec[0][0].c_str());
        //write(sock, outBuf, strlen(outBuf));
        exit(0);
      } 
    }else{ // parent
      waitpid(pid, NULL, 0);
    }
    return;
  }

  // vector<int> predecessor = getPredecessor(iLine);  
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
        map<int, vector<int>>::iterator mit;
        mit = mapSuccessor.find(iLine);
        if (mit != mapSuccessor.end()){ // has predecessor
          prevPipeOutput = mapSuccessor[iLine][0];
          dup2(prevPipeOutput, STDIN_FILENO); // stdin from previous cmd
          close(prevPipeOutput);
          close(mapSuccessor[iLine][1]);
        }
        if(successor[iLine] != -1 && icmd == cmdVec.size()-1){
          // numpipe && last cmd
          if (sharePipeFlag){
            dup2(mapSuccessor[successor[iLine]][1], STDOUT_FILENO); // output to shared pipe
            if (pipeErrFlag){
              int tgCopy = dup(mapSuccessor[successor[iLine]][1]);
              dup2(tgCopy, STDERR_FILENO); // output to shared pipe
              close(tgCopy);
            }
            close(mapSuccessor[successor[iLine]][1]);
          }else {
            dup2(pfd[1], STDOUT_FILENO); // output to pipe
            if (pipeErrFlag){
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
        if (successor[iLine] != -1 && icmd == cmdVec.size()-1){
          // |n or !n & last cmd
          if (sharePipeFlag){
            dup2(mapSuccessor[successor[iLine]][1], STDOUT_FILENO); // output to shared pipe
            if (pipeErrFlag){
              int tgCopy = dup(mapSuccessor[successor[iLine]][1]);
              dup2(tgCopy, STDERR_FILENO); // output to shared pipe
              close(tgCopy);
            }
            close(mapSuccessor[successor[iLine]][1]);
          }else {
            dup2(pfd[1], STDOUT_FILENO); // output to pipe
            if (pipeErrFlag){
              int tgCopy = dup(pfd[1]);
              dup2(tgCopy, STDERR_FILENO); // output to pipe
              close(tgCopy);
            }
          }
        }else {
          dup2(pfd[1], STDOUT_FILENO); // output to pipe
        }
      }
      int nfds = getdtablesize();
      for (int fd = 3; fd <= nfds; fd++){
        if (fd != prevPipeOutput){
          close(fd);
        }
      }
      close(pfd[1]);

      if(execvp(curCmd[0].c_str(), vecStrToChar(curCmd)) == -1){
        cerr << "Unknown command: [" << curCmd[0] << "]." << strerror(errno) << endl;
        //sprintf(outBuf, "Unknown command: [%s].", cmdVec[0][0].c_str());
        //write(sock, outBuf, strlen(outBuf));
        close(pfd[1]); // necessary?
        exit(0);
      }
    } /* parent process */
    else {
      // cout << "create pfd: " << pfd[0] << " " << pfd[1] << endl; 
      if (successor[iLine] != -1 && !sharePipeFlag && icmd == cmdVec.size()-1){
        // numpipe && pass to new successor && last cmd
        mapSuccessor[successor[iLine]][1] = pfd[1];
      }else {
        close(pfd[1]);
        // cout << "parent close [" << pfd[1] << "]" << endl;
      }
      childPids.push_back(pid);
      if (icmd != 0){
        close(prevPipeOutput);
      }
      prevPipeOutput = pfd[0];
    }
  }
  outLinePfd[iLine] = pfd[0];
  mit = mapSuccessor.find(iLine);
  if (mit != mapSuccessor.end()){
    close(mapSuccessor[iLine][0]);
    close(mapSuccessor[iLine][1]);
    // cout << "parent close [" << mapSuccessor[iLine][0] << "]" << endl;
    // cout << "parent close [" << mapSuccessor[iLine][1] << "]" << endl;
  }
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
  while(waitpid(-1, NULL, WNOHANG) > 0){

  }
}
