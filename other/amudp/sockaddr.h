//  $Archive:: /Ti/AMUDP/sockaddr.h                                       $
//     $Date: 2003/12/11 20:19:53 $
// $Revision: 1.1 $
// Description: Objects for encapsulating and hashing SockAddr's
// Copyright 1998, Dan Bonachea


#ifndef _SOCKADDR_H
#define _SOCKADDR_H

#include "socket.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define LOCALHOST (u_long)0x7F000001

#if !defined(AMUDP_NDEBUG) && !defined(NDEBUG)
#define DEBUGIP() this->IPStr(); 
#else
#define DEBUGIP() ; 
#endif 

class SockAddr {
  private:
    sockaddr addr; // always stored in network byte order
    char IPBuffer[17];

  public:
    SockAddr() {
      memset(&addr,'\0',sizeof(sockaddr));
      }
    SockAddr(unsigned long IPaddr, unsigned short portnum, short sin_family=AF_INET) {
      ((sockaddr_in*)&addr)->sin_family = sin_family;
      ((sockaddr_in*)&addr)->sin_port = htons(portnum); // change to network format port
      ((sockaddr_in*)&addr)->sin_addr.s_addr = htonl(IPaddr); // change to network format
      memset(&((sockaddr_in*)&addr)->sin_zero, '\0', sizeof(((sockaddr_in*)&addr)->sin_zero));      
      DEBUGIP(); // makes it easier to see addresses in debugger
      }
    SockAddr(const char* IPStr, unsigned short portnum, short sin_family=AF_INET) { 
      ((sockaddr_in*)&addr)->sin_family = sin_family;
      ((sockaddr_in*)&addr)->sin_port = htons(portnum); // change to network format port
      ((sockaddr_in*)&addr)->sin_addr.s_addr = inet_addr(IPStr); // already in network format
      memset(&((sockaddr_in*)&addr)->sin_zero, '\0', sizeof(((sockaddr_in*)&addr)->sin_zero));
      DEBUGIP(); // makes it easier to see addresses in debugger
      }
    SockAddr(const char* IPPortStr) { // parses from an FTP 227 or PORT string
      int num[6] = {0,0,0,0,0,0};
      while(!isdigit(*IPPortStr)) IPPortStr++; // skip leading text
      for (int i=0; i < 6 && *IPPortStr; i++) { // read each digit
        num[i] = atoi(IPPortStr);
        while(isdigit(*IPPortStr)) IPPortStr++; // skip digit
        while(!isdigit(*IPPortStr) && *IPPortStr) IPPortStr++; // skip separator
        }
      unsigned short portnum;
      char IPStr[30];
      sprintf(IPStr, "%i.%i.%i.%i", num[0], num[1], num[2], num[3]); 
      portnum = (unsigned short)(num[4] << 8 | num[5]);
      ((sockaddr_in*)&addr)->sin_family = AF_INET;
      ((sockaddr_in*)&addr)->sin_port = htons(portnum); // change to network format port
      ((sockaddr_in*)&addr)->sin_addr.s_addr = inet_addr(IPStr); // already in network format
      memset(&((sockaddr_in*)&addr)->sin_zero, '\0', sizeof(((sockaddr_in*)&addr)->sin_zero));
      DEBUGIP(); // makes it easier to see addresses in debugger
      }
    SockAddr(const sockaddr* sa) {
      memcpy(&addr, sa, sizeof(sockaddr));
      DEBUGIP(); // makes it easier to see addresses in debugger
      }
    SockAddr(const sockaddr_in* sa) {
      memcpy(&addr, sa, sizeof(sockaddr));
      DEBUGIP(); // makes it easier to see addresses in debugger
      }
    SockAddr(unsigned short _sa_family, char _sa_data[14]) {
      addr.sa_family = _sa_family;
      memcpy(&addr.sa_data, _sa_data, sizeof(addr.sa_data));
      DEBUGIP(); // makes it easier to see addresses in debugger
      }
    operator sockaddr*() { return &addr; }
    operator sockaddr_in*() { return (sockaddr_in *)&addr; }
    unsigned long IP() { return ntohl(((sockaddr_in*)&addr)->sin_addr.s_addr); }

    unsigned short port() { return ntohs(((sockaddr_in*)&addr)->sin_port); }
    char *IPStr() { 
      // inet_ntoa(((sockaddr_in*)&addr)->sin_addr); // This may screw up transparency
      unsigned int a = ((unsigned char*)addr.sa_data)[2], b = ((unsigned char*)addr.sa_data)[3],
          c = ((unsigned char*)addr.sa_data)[4], d = ((unsigned char*)addr.sa_data)[5];
      sprintf(IPBuffer, "%u.%u.%u.%u",a,b,c,d);
      return IPBuffer; 
      }
    char *FTPStr() { 
      unsigned int a = ((unsigned char*)addr.sa_data)[2], b = ((unsigned char*)addr.sa_data)[3],
          c = ((unsigned char*)addr.sa_data)[4], d = ((unsigned char*)addr.sa_data)[5];
      unsigned short prt = port();
      unsigned int e = (prt >> 8), 
                   f = (prt & 0xFF);
      sprintf(IPBuffer, "%u,%u,%u,%u,%u,%u",a,b,c,d,e,f);
      return IPBuffer; 
      }
    int operator==(const SockAddr& other) const { return !memcmp(this, &other.addr, sizeof(sockaddr)); }
    int operator!=(const SockAddr& other) const { return !(*this == other); }
  };

#endif
