/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <Python.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cygwin.h>

#include <windows.h>
#include <lm.h>
#include <winnetwk.h>

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

#define MAX_USERBUFFER_SIZE 1024

static char userinfo[MAX_USERBUFFER_SIZE] = { 0 };
static char * logonuser = NULL;
static char * logondomain = NULL;
static char * logonserver = NULL;

static size_t
wchar2mchar(wchar_t *ws, char *buffer, size_t size)
{
  size_t len;
  len = WideCharToMultiByte(CP_ACP,
                            0,
                            ws,
                            -1,
                            NULL,
                            0,
                            NULL,
                            NULL
                            );
  if (len + 1 > size)
    return -1;
  if (WideCharToMultiByte(CP_ACP,
                          0,
                          ws,
                          -1,
                          buffer,
                          len,
                          NULL,
                          NULL
                          ) == 0)
    return -1;
  return len + 1;
}

static PyObject *
netuse_user_info(PyObject *self, PyObject *args)
{
  DWORD dwLevel = 1;
  LPWKSTA_USER_INFO_1 pBuf = NULL;
  NET_API_STATUS nStatus;
  //
  // Call the NetWkstaUserGetInfo function;
  //  specify level 1.
  //
  nStatus = NetWkstaUserGetInfo(NULL,
                                dwLevel,
                                (LPBYTE *)&pBuf);
  //
  // If the call succeeds, print the information
  //  about the logged-on user.
  //
  if (nStatus == NERR_Success) {
    if (pBuf != NULL) {
      size_t size = MAX_USERBUFFER_SIZE;
      size_t len;
      logonuser = userinfo;
      len = wchar2mchar(pBuf->wkui1_username, logonuser, size);
      if (len == -1) {
        PyErr_SetString(PyExc_RuntimeError, "Unicode conversion error");
        return NULL;
      }
      size -= len;
      logondomain = logonuser + len;
      len = wchar2mchar(pBuf->wkui1_logon_domain, logondomain, size);
      if (len == -1) {
        PyErr_SetString(PyExc_RuntimeError, "Unicode conversion error");
        return NULL;
      }
      size -= len;
      logonserver = logondomain + len;
      len = wchar2mchar(pBuf->wkui1_logon_server, logonserver, size);
      if (len == -1) {
        PyErr_SetString(PyExc_RuntimeError, "Unicode conversion error");
        return NULL;
      }
    }
  }
  // Otherwise, print the system error.
  //
  else {
    PyErr_Format(PyExc_RuntimeError,
                 "A system error has occurred: %ld",
                 nStatus
                 );
    return NULL;
  }
  //
  // Free the allocated memory.
  //
  if (pBuf != NULL) {
    NetApiBufferFree(pBuf);
    return Py_BuildValue("sss", logonserver, logondomain, logonuser);
  }

  PyErr_SetString(PyExc_RuntimeError, "No logon user information");
  return NULL;
}

static int
wnet_enumerate_netdrive(LPNETRESOURCE lpnr)
{
  DWORD dwResult, dwResultEnum;
  HANDLE hEnum;
  DWORD cbBuffer = 16384;     // 16K is a good size
  DWORD cEntries = -1;        // enumerate all possible entries
  LPNETRESOURCE lpnrLocal;    // pointer to enumerated structures
  DWORD i;
  dwResult = WNetOpenEnum(RESOURCE_GLOBALNET,
                          RESOURCETYPE_DISK,
                          0,
                          lpnr,       // NULL first time the function is called
                          &hEnum);    // handle to the resource

  if (dwResult != NO_ERROR) {
    printf("WnetOpenEnum failed with error %ld\n", dwResult);
    return FALSE;
  }
  lpnrLocal = (LPNETRESOURCE) GlobalAlloc(GPTR, cbBuffer);
  if (lpnrLocal == NULL) {
    printf("WnetOpenEnum failed with error %ld\n", dwResult);
    return FALSE;
  }

  do {
    ZeroMemory(lpnrLocal, cbBuffer);
    dwResultEnum = WNetEnumResource(hEnum,     // resource handle
                                    &cEntries, // defined locally as -1
                                    lpnrLocal, // LPNETRESOURCE
                                    &cbBuffer);
    if (dwResultEnum == NO_ERROR) {
      for (i = 0; i < cEntries; i++) {
        printf("NETRESOURCE[%ld] Usage: 0x%ld = ", i, lpnrLocal[i].dwUsage);
        if (lpnrLocal[i].dwUsage & RESOURCEUSAGE_CONNECTABLE)
          printf("connectable ");
        if (lpnrLocal[i].dwUsage & RESOURCEUSAGE_CONTAINER)
          printf("container ");
        printf("\n");

        printf("NETRESOURCE[%ld] Localname: %s\n", i, lpnrLocal[i].lpLocalName);
        printf("NETRESOURCE[%ld] Remotename: %s\n", i, lpnrLocal[i].lpRemoteName);
        printf("NETRESOURCE[%ld] Comment: %s\n", i, lpnrLocal[i].lpComment);
        printf("NETRESOURCE[%ld] Provider: %s\n", i, lpnrLocal[i].lpProvider);
        printf("\n");
        if (RESOURCEUSAGE_CONTAINER == (lpnrLocal[i].dwUsage
                                        & RESOURCEUSAGE_CONTAINER))
          if (!wnet_enumerate_netdrive(&lpnrLocal[i]))
            printf("EnumerateFunc returned FALSE\n");

      }
    }
    else if (dwResultEnum != ERROR_NO_MORE_ITEMS) {
      printf("WNetEnumResource failed with error %ld\n", dwResultEnum);
      break;
    }
  } while (dwResultEnum != ERROR_NO_MORE_ITEMS);

  GlobalFree((HGLOBAL) lpnrLocal);
  dwResult = WNetCloseEnum(hEnum);

  if (dwResult != NO_ERROR) {
    printf("WNetCloseEnum failed with error %ld\n", dwResult);
    return FALSE;
  }
  return TRUE;
}

static int
reg_enumerate_netdrive(void)
{
  /* LsaEnumerateLogonSessions -> LogonId */
  /* OpenTokenByLogonId ->  TokenHandle */
  /* LsaGetLogonSessionData -> Sid */
  /* ConvertSidToStringSid */
  /* From regkey HKU\SID\NETWORK */
  /* ImpersonateLoggedOnUser Or CreateProcessWithTokenW */

  /* WNetAddConnection */
  return 0;
}

static PyObject *
netuse_list_drive(PyObject *self, PyObject *args)
{
  PyObject *retvalue = NULL;
  PyObject *pobj = NULL;
  char *servername = NULL;
  char *username = NULL;
  char *password = NULL;
  char chdrive = 'A';
  char drivepath[] = { 'A', ':', '\\', 0 };
  char drivename[] = { 'A', ':', 0 };

  char szRemoteName[MAX_PATH];
  DWORD dwResult;
  DWORD cchBuff = MAX_PATH;
  char szUserName[MAX_PATH] = {0};

  if (! PyArg_ParseTuple(args, "|s", &servername)) {
    return NULL;
  }

  retvalue = PyList_New(0);
  if (retvalue == NULL)
    return NULL;

  while (chdrive <= 'Z') {
    drivepath[0] = chdrive;
    drivename[0] = chdrive;

    dwResult = WNetGetConnection(drivename,
                                 szRemoteName,
                                 &cchBuff
                                 );
    if (dwResult == NO_ERROR) {
      dwResult = WNetGetUser(drivename,
                             (LPSTR) szUserName,
                             &cchBuff);
      if (dwResult != NO_ERROR)
        snprintf(szUserName, MAX_PATH, "%s", "Unknown User");
      pobj = Py_BuildValue("ssss",
                           drivename,
                           szRemoteName,
                           "OK",
                           szUserName
                           );
      if (PyList_Append(retvalue, pobj) == -1) {
        Py_XDECREF(retvalue);
        return NULL;
      }
    }
    else if (dwResult == ERROR_CONNECTION_UNAVAIL) {
    }
    else if (dwResult == ERROR_NOT_CONNECTED) {
    }
    else if (dwResult == ERROR_BAD_DEVICE) {
    }
    else if (dwResult == ERROR_NO_NET_OR_BAD_PATH) {
    }
    else {
      PyErr_Format(PyExc_RuntimeError,
                   "A system error has occurred in WNetGetConnection: %ld",
                   GetLastError()
                   );
      Py_XDECREF(retvalue);
      return NULL;
    }
    ++ chdrive;
  }
  return retvalue;
}

static int
connect_net_drive(char *remote, char *drive)
{
  DWORD dwRetVal;
  NETRESOURCE nr;
  DWORD dwFlags;
  char *password=NULL;
  char *user=NULL;

  memset(&nr, 0, sizeof (NETRESOURCE));
  nr.dwType = RESOURCETYPE_DISK;
  nr.lpLocalName = drive;
  nr.lpRemoteName = remote;
  nr.lpProvider = NULL;

  dwFlags = CONNECT_REDIRECT;
  dwRetVal = WNetAddConnection2(&nr, password, user, dwFlags);
  if (dwRetVal == NO_ERROR)
    return 0;
  PyErr_Format(PyExc_RuntimeError,
               "WNetAddConnection2 failed with error: %lu\n",
               dwRetVal
               );
  return dwRetVal;
}

static PyObject *
netuse_auto_connect(PyObject *self, PyObject *args)
{
  Py_RETURN_NONE;
}

static PyObject *
netuse_remove_drive(PyObject *self, PyObject *args)
{
  DWORD dwRetVal;
  char *drive = NULL;
  int force = 1;

  if (! PyArg_ParseTuple(args, "si", &drive, &force))
    return NULL;

  dwRetVal = WNetCancelConnection2(drive, 0, force);
  if (dwRetVal == NO_ERROR)
    Py_RETURN_NONE;

  PyErr_Format(PyExc_RuntimeError,
               "WNetCancelConnection2 failed with error: %lu\n",
               dwRetVal
               );
  return NULL;
}

static PyObject *
netuse_map_drive(PyObject *self, PyObject *args)
{
  DWORD dwRetVal;
  NETRESOURCE nr;
  DWORD dwFlags;

  char *remote = NULL;
  char *drive = NULL;
  char *user = NULL;
  char *password = NULL;
  char accessName[MAX_PATH] = {0};
  DWORD dwBufSize = MAX_PATH;
  DWORD dwResult;

  if (! PyArg_ParseTuple(args, "ss|ss", &remote, &drive, &user, &password))
    return NULL;

  memset(&nr, 0, sizeof (NETRESOURCE));
  nr.dwType = RESOURCETYPE_DISK;
  nr.lpLocalName = drive;
  nr.lpRemoteName = remote;
  nr.lpProvider = NULL;

  dwFlags = CONNECT_UPDATE_PROFILE;
  if (drive == NULL)
    dwFlags |= CONNECT_REDIRECT;
  dwRetVal = WNetUseConnection(NULL,
                               &nr,
                               password,
                               user,
                               dwFlags,
                               accessName,
                               &dwBufSize,
                               &dwResult);
  if (dwRetVal == NO_ERROR)
    return PyString_FromString(accessName);
  PyErr_Format(PyExc_RuntimeError,
               "WNetAddConnection2 failed with error: %lu\n",
               dwRetVal
               );
  return NULL;
}

static PyObject *
netuse_usage_report(PyObject *self, PyObject *args)
{
  char *drive = NULL;
  PyObject *pobj = NULL;

  ULARGE_INTEGER lFreeBytesAvailable;
  ULARGE_INTEGER lTotalNumberOfBytes;
  /* ULARGE_INTEGER lTotalNumberOfFreeBytes; */

  if (! PyArg_ParseTuple(args, "s", &drive))
    return NULL;

  if (GetDiskFreeSpaceEx(drive,
                         &lFreeBytesAvailable,
                         &lTotalNumberOfBytes,
                         NULL
                         )) {
    pobj = Py_BuildValue("LL", lFreeBytesAvailable, lTotalNumberOfBytes);
    return pobj;
  }

  PyErr_Format(PyExc_RuntimeError,
               "A system error has occurred in GetDiskFreeSpaceEx(%s): %ld",
               drive,
               GetLastError()
               );
  return NULL;
}

/* Useless code */
#if 0

static char
get_free_drive_letter(void)
{
  DWORD bitmasks = GetLogicalDrives();
  char ch = 'A';
  while (bitmasks) {
    if ((bitmasks & 1L) == 0)
      return ch;
    ++ ch;
    bitmasks >>= 1;
  }
  return (char)0;
}

/*
 * Travel all the mapped drive to check whether there is duplicated
 * shared folder:
 *
 *   Return 1 if current share folder is same or sub-folder of the
 *   mapped folder;
 *
 *   Return -1 if unknown exception occurs;
 *
 *   Remove mapped item from list if the mapped folder is sub-folder
 *   of current share folder.
 *
 * Return 0 if it's new share folder.
 *
 */
static int
check_duplicate_shared_folder(PyObject *retvalue, const char *folder)
{
  if (!PyList_Check(retvalue))
    return -1;

  Py_ssize_t size = PyList_Size(retvalue);
  int len = strlen(folder);
  int len2;
  PyObject *item;
  char * s;

  while (size > 0) {
    size --;
    item = PySequence_GetItem(retvalue, size);
    if (!PySequence_Check(item))
      return -1;
    s = PyString_AsString(PySequence_GetItem(item, 1));
    if (s == NULL)
      return -1;
    len2 = strlen(s);

    if (strncmp(folder, s, len > len2 ? len : len2) == 0) {
      if (len2 > len) {
        if (PySequence_DelItem(retvalue, size) == -1)
          return -1;
        size --;
      }
      else
        return 1;
    }
  }
  return 0;
}

static PyObject *
netuse_usage_report_orig(PyObject *self, PyObject *args)
{
  char * servername = NULL;
  PyObject *retvalue = NULL;
  DWORD bitmasks;
  char chdrive = '@';
  char  drivepath[] = { 'A', ':', '\\', 0 };
  char  drivename[] = { 'A', ':', 0 };
  ULARGE_INTEGER lFreeBytesAvailable;
  ULARGE_INTEGER lTotalNumberOfBytes;
  /* ULARGE_INTEGER lTotalNumberOfFreeBytes; */

  char szRemoteName[MAX_PATH];
  DWORD dwResult, cchBuff = MAX_PATH;
  DWORD serverlen = 0;

  if (! PyArg_ParseTuple(args, "|s", &servername)) {
    return NULL;
  }

  if (servername)
    serverlen = strlen(servername);

  bitmasks = GetLogicalDrives();
  if (bitmasks == 0) {
     PyErr_Format(PyExc_RuntimeError,
                  "A system error has occurred in GetLogicalDrives: %ld",
                  GetLastError()
                  );
     return NULL;
  }

  retvalue = PyList_New(0);
  if (retvalue == NULL)
    return NULL;

  while (bitmasks) {
    ++ chdrive;
    drivepath[0] = chdrive;
    drivename[0] = chdrive;

    if ((bitmasks & 1L) == 0) {
      bitmasks >>= 1;
      continue;
    }

    bitmasks >>= 1;
    switch (GetDriveType(drivepath)) {
      case DRIVE_REMOTE:
        break;
      case DRIVE_FIXED:
        continue;
      default:
        continue;
      }

    /* If the network connection was made using the Microsoft LAN
     * Manager network, and the calling application is running in a
     * different logon session than the application that made the
     * connection, a call to the WNetGetConnection function for the
     * associated local device will fail. The function fails with
     * ERROR_NOT_CONNECTED or ERROR_CONNECTION_UNAVAIL. This is
     * because a connection made using Microsoft LAN Manager is
     * visible only to applications running in the same logon session
     * as the application that made the connection. (To prevent the
     * call to WNetGetConnection from failing it is not sufficient for
     * the application to be running in the user account that created
     * the connection.)
     *
     * Refer to http://msdn.microsoft.com/en-us/library/windows/desktop/aa385453(v=vs.85).aspx
     *
     */
    dwResult = WNetGetConnection(drivename,
                                 szRemoteName,
                                 &cchBuff
                                 );
    if (dwResult == NO_ERROR || dwResult == ERROR_CONNECTION_UNAVAIL    \
        || dwResult == ERROR_NOT_CONNECTED) {
      if (serverlen) {
        if ((cchBuff < serverlen + 3) ||
            (strncmp(servername, szRemoteName+2, serverlen) != 0) ||
            (szRemoteName[serverlen + 2] != '\\')
            )
        continue;
      }
    }

    else {
      PyErr_Format(PyExc_RuntimeError,
                   "A system error has occurred in WNetGetConnection: %ld",
                   GetLastError()
                   );
      Py_XDECREF(retvalue);
      return NULL;
    }

    switch (check_duplicate_shared_folder(retvalue, szRemoteName)) {
    case -1:
      Py_XDECREF(retvalue);
      return NULL;
    case 1:
      continue;
    default:
      break;
    }

    if (GetDiskFreeSpaceEx(drivepath,
                           &lFreeBytesAvailable,
                           &lTotalNumberOfBytes,
                           NULL
                           )) {
      PyObject *pobj = Py_BuildValue("ssLL",
                                     drivename,
                                     szRemoteName,
                                     lFreeBytesAvailable,
                                     lTotalNumberOfBytes
                                     );
      if (PyList_Append(retvalue, pobj) == -1) {
        Py_XDECREF(retvalue);
        return NULL;
      }
    }
    else if (dwResult == ERROR_CONNECTION_UNAVAIL || dwResult == ERROR_NOT_CONNECTED) {
      PyObject *pobj = Py_BuildValue("ssLL",
                                     drivename,
                                     szRemoteName,
                                     0L,
                                     0L
                                     );
      if (PyList_Append(retvalue, pobj) == -1) {
        Py_XDECREF(retvalue);
        return NULL;
      }
    }
    else {
     PyErr_Format(PyExc_RuntimeError,
                  "A system error has occurred in GetDiskFreeSpaceEx(%s): %ld",
                  drivepath,
                  GetLastError()
                  );
     Py_XDECREF(retvalue);
     return NULL;
    }
  }
  return retvalue;
}
#endif  /* #if 0 */

static PyMethodDef NetUseMethods[] = {
  {
    "autoConnect",
    netuse_auto_connect,
    METH_VARARGS,
    (
     "autoConnect()\n\n"
     "Create mapped drive from shared folder, it uses the default user\n"
     "name. (provided by the user context for the process.)\n"
     "Raise exception if something is wrong.\n "
     )
  },
  {
    "listNetDrive",
    netuse_list_drive,
    METH_VARARGS,
    (
     "listNetDrive()\n\n"
     "List all the net drives visible by current user. Return a list:\n"
     "  [ (drive, remote, status, user), ... ] \n"
     "Not that if the calling application is running in a different logon\n"
     "session than the application that made the connection, it's\n"
     "unvisible for the current application. If the current user has\n"
     "administrator privilege, these connections could be shown, but\n"
     "the status is unavaliable or unconnect.\n"
     "\n"
     "Refer to http://msdn.microsoft.com/en-us/library/windows/desktop/aa363908(v=vs.85).aspx\n"
     "Defining an MS-DOS Device Name\n"
     "Refer to http://msdn.microsoft.com/en-us/library/windows/hardware/ff554302(v=vs.85).aspx\n"
     "Local and Global MS-DOS Device Names\n"
     )
  },
  {
    "mapNetDrive",
    netuse_map_drive,
    METH_VARARGS,
    (
     "mapNetDrive(remote, drive, user=None, password=None)\n\n"
     "Create net drive from remote folder, and return the assigned\n"
     "drive letter. \n"
     "It uses the default user which initialize this remote connection\n"
     "if user is None. \n"
     "When drive is an empty string, the system will automatically\n"
     "assigns network drive letters, letters are assigned beginning\n"
     "with Z:, then Y:, and ending with C:\n."
     "For examples,\n"
     "  mapNetDrive(r'\\\\server\\data')\n"
     "  mapNetDrive(r'\\\\server\\data', 'T:')\n"
     "  mapNetDrive(r'\\\\server\\data', 'T:', r'\\\\server\\jack', 'abc')\n"
     "Raise exception if something is wrong.\n"
     )
  },
  {
    "removeNetDrive",
    netuse_remove_drive,
    METH_VARARGS,
    (
     "removeNetDrive(drive, force=True)\n\n"
     "Remove mapped drive specified by drive, For example,\n"
     "  removeNetDrive('X:')\n"
     "Parameter force specifies whether the disconnection should occur\n"
     "if there are open files or jobs on the connection. If this parameter\n"
     "is FALSE, the function fails if there are open files or jobs.\n"
     "Raise exception if something is wrong, otherwise return None.\n"
     )
  },
  {
    "usageReport",
    netuse_usage_report,
    METH_VARARGS,
    (
     "usageReport(drive)\n\n"
     "Return a tuple to report the usage of the net drive:\n"
     "  (available, total)\n"
     "For examples,\n"
     "  usageReport('Z:')\n"
     "Raise exception if something is wrong.\n"
     )
  },
  {
    "userInfo",
    netuse_user_info,
    METH_VARARGS,
    (
     "userInfo()\n\n"
     "Get the logon user information, return a tuple:\n"
     "  (server, domain, user).\n"
     )
  },
  {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initnetuse(void)
{
  PyObject* module;
  module = Py_InitModule3("netuse",
                          NetUseMethods,
                          "Show information about net resource in Windows."
                          );

  if (module == NULL)
    return;
}
