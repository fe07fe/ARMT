/*
 * ARMT (Another Remote Monitoring Tool)
 * Copyright (C) Geoffrey McRae 2012 <geoff@spacevs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/utsname.h>

#include <iostream>
#include <fstream>

#include "common/CDNS.h"
#include "common/CPCIInfo.h"
#include "common/CProcInfo.h"
#include "common/CCommon.h"
#include "common/CHTTP.h"
#include "common/CCompress.h"
#include "common/CMessageBuilder.h"

#include "fs/CFSVerifier.h"
#include "block/CBlockEnumerator.h"

/* we wrap the libc version to avoid shared linking */
CDNS DNS;
static struct hostent *_hostent = NULL;
extern "C" {
  struct hostent *__wrap_gethostbyname(const char *host)
  {
    if (_hostent)
      DNS.Free(_hostent);
    _hostent = DNS.GetHostByName(host);
    return _hostent;
  }
}

bool DISKCHECK(std::iostream &ss)
{
  bool send = false;

  IBlockDevice::Map map = CBlockEnumerator::Enumerate();
  for(IBlockDevice::Map::iterator it = map.begin(); it != map.end(); ++it)
  {
    const std::string &error = it->second->GetError();
    if (!error.empty())
      continue;

    /* dont send devices that are not faulting */
    if (it->second->IsOK())
      continue;

    send = true;
    CMessageBuilder::PackString(ss, it->second->GetType        ());
    CMessageBuilder::PackString(ss, it->second->GetDevName     ());
    CMessageBuilder::PackString(ss, it->second->GetModel       ());
    CMessageBuilder::PackString(ss, it->second->GetSerialNumber());
    CMessageBuilder::PackString(ss, it->second->GetFirmware    ());   
  }

  return send;
}

bool FSCHECK(std::iostream &ss)
{
  CFSVerifier fs;
  fs.AddExclude("/boot/lost+found");
  fs.AddExclude("/usr/src");
  fs.AddExclude("/lib/init/rw");

  /* get the running kernel version and add the module path for it */
  struct utsname details;
  uname(&details);
  std::string modules = "/lib/modules/";
  modules.append(details.release);
  fs.AddPath(modules, true);

  /* exclude all other module paths */
  fs.AddExclude("/lib/modules");

  /* protect critcal paths */
  fs.AddPath("/boot", true);
  fs.AddPath("/bin" , true);
  fs.AddPath("/sbin", true);
  fs.AddPath("/lib" , true);

  /* scan for files and hash them */
  fs.Scan();
  return fs.Save(ss);
}

int main(int argc, char *argv[])
{
  /* must be called first */
  CCommon::Initialize(argc, argv);

  /* DNS resolver must be setup next so the wrapper works */
  DNS.AddResolver("8.8.8.8"       );  /* Google  */
  DNS.AddResolver("8.8.4.4"       );  /* Google  */
  DNS.AddResolver("208.67.222.222");  /* OpenDNS */
  DNS.AddResolver("208.67.222.220");  /* OpenDNS */

  std::string  armthost;
  unsigned int armtport;
  switch(argc)
  {
    case 2:
      armthost = argv[1];
      armtport = 443;
      break;

    case 3:
      armthost = argv[1];
      armtport = strtoul(argv[2], NULL, 10);
      break;

    default:
      std::cerr << "Usage: " << argv[0] << " armt.host.com [port]" << std::endl;
      return -1;
  }

  CMessageBuilder msg(armthost, armtport);
  msg.AppendSegment("DISKCHECK", &DISKCHECK);
  msg.AppendSegment("FSCHECK"  , &FSCHECK  );
  msg.Send();

  if (_hostent)
    DNS.Free(_hostent);

  return 0;

#if 0
  CProcInfo::ProcessList list = CProcInfo::GetProcessList();
  for(CProcInfo::ProcessListIterator itt = list.begin(); itt != list.end(); ++itt)
  {  
    printf(
      "PID    : %d\n"
      "EXE    : %s\n"
      "COMM   : %s\n"
      "CMDLINE: %s\n",
      itt->GetPID(),
      itt->GetExe    ().c_str(),
      itt->GetComm   ().c_str(),
      itt->GetCmdLine().c_str()
    );
    
    CProcInfo::BoundList bound = itt->GetBoundList();
    for(CProcInfo::BoundListIterator itt2 = bound.begin(); itt2 != bound.end(); ++itt2)
    {
      if (itt2->GetType() == CProcInfo::PORT_TYPE_UNIX)
           printf("  BOUND: %s(%s)\n"   , CProcInfo::PortTypeToString(itt2->GetType()), itt2->GetPath().c_str());
      else printf("  BOUND: %s(%s:%u)\n", CProcInfo::PortTypeToString(itt2->GetType()), CProcInfo::IPToString(itt2->GetIP()).c_str(), itt2->GetPort());
    }
    
    printf("\n");
  }
#endif
}
