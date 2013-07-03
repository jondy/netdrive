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
        PyErr_SetString(PyExc_RuntimeError, "Unicode convertion error");
        return NULL;
      }
      size -= len;
      logondomain = logonuser + len;
      len = wchar2mchar(pBuf->wkui1_logon_domain, logondomain, size);
      if (len == -1) {
        PyErr_SetString(PyExc_RuntimeError, "Unicode convertion error");
        return NULL;
      }
      size -= len;
      logonserver = logondomain + len;
      len = wchar2mchar(pBuf->wkui1_logon_server, logonserver, size);
      if (len == -1) {
        PyErr_SetString(PyExc_RuntimeError, "Unicode convertion error");
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

static char
get_free_drive_letter()
{
  DWORD bitmasks = GetLogicalDrives();
  char ch = 'A';
  while (bitmasks) {
    if ((bitmasks & 1L) == 0)
      return ch;
    ++ ch;
    bitmasks >>= 1;
  }
  return 0;
}

static PyObject *
netuse_map_drive(PyObject *self, PyObject *args)
{
  DWORD dwRetVal;
  NETRESOURCE nr;
  DWORD dwFlags;

  char *remote = NULL;
  char drive[] = { 0, ':', 0 };
  char *user = NULL;
  char *password = NULL;
  if (! PyArg_ParseTuple(args, "s", &remote)) {
    return NULL;
  }

  drive[0] = get_free_drive_letter();
  if (!drive[0]) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Add net drive faild: no available drive letter."
                    );
    return NULL;
  }

  memset(&nr, 0, sizeof (NETRESOURCE));
  nr.dwType = RESOURCETYPE_DISK;
  nr.lpLocalName = drive;
  nr.lpRemoteName = remote;
  nr.lpProvider = NULL;

  dwFlags = CONNECT_UPDATE_PROFILE;
  dwRetVal = WNetAddConnection2(&nr, password, user, dwFlags);
  if (dwRetVal == NO_ERROR)
    return PyString_FromString(drive);
  PyErr_Format(PyExc_RuntimeError,
               "WNetAddConnection2 failed with error: %lu\n",
               dwRetVal
               );
  return NULL;
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
netuse_usage_report(PyObject *self, PyObject *args)
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
      case DRIVE_FIXED:
      case DRIVE_REMOTE:
        break;
      default:
        continue;
      }

    dwResult = WNetGetConnection(drivename,
                                 szRemoteName,
                                 &cchBuff
                                 );
    if (dwResult == NO_ERROR) {
      if (serverlen) {
        if ((cchBuff < serverlen + 3) ||
            (strncmp(servername, szRemoteName+2, serverlen) != 0) ||
            (szRemoteName[serverlen + 2] != '\\')
            )
        continue;
      }
    }

    // The device is not currently connected, but it is a persistent connection.
    else if (dwResult == ERROR_CONNECTION_UNAVAIL) {
      continue;
    }

    // The device is not a redirected device.
    else if (dwResult == ERROR_NOT_CONNECTED) {
      continue;
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

static PyMethodDef NetUseMethods[] = {
  {
    "userInfo",
    netuse_user_info,
    METH_VARARGS,
    (
     "userInfo()\n\n"
     "Get the logon user information, return a tuple:\n"
     "(server, domain, user).\n"
     )
  },
  {
    "mapDrive",
    netuse_map_drive,
    METH_VARARGS,
    (
     "mapDrive(sharefolder)\n\n"
     "Create mapped drive from shared folder, it uses the default user\n"
     "name. (provided by the user context for the process.) \n"
     )
  },
  {
    "usageReport",
    netuse_usage_report,
    METH_VARARGS,
    (
     "usagereport(servername='')\n\n"
     "Return a tuple to report all the net drive information:\n"
     "[ (drive, remote, available, total), ... ]\n"
     "If servername is not empty, then only net drives in the specified server\n"
     "are returned.\n"
     )
  },
  {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initnetuse(void)
{
  PyObject* module;
  module = Py_InitModule3("netuse",
                          NetUseMethods,
                          "Show information about net resource in the Windows."
                          );

  if (module == NULL)
    return;
}
