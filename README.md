NetDrive
========

A tool used to get netdrive usage by each account in the Windows Platform.


Usage
=====

The mainly functions are found in the python extension: netuse

python setup.py build

Or you can use your prefer way to build netuse.c, get the extension: netuse

It exports the following functions:

autoConnect()
-------------

Create mapped drive from shared folder, it uses the default user
name. (provided by the user context for the process.)  

Raise exception if something is wrong.

listNetDrive()
--------------

List all the net drives visible by current user. Return a list:

  [ (drive, remote, status, user), ... ] 

Not that if the calling application is running in a different logon
session than the application that made the connection, it's unvisible
for the current application. If the current user has administrator
privilege, these connections could be shown, but the status is
unavaliable or unconnect.

Refer to http://msdn.microsoft.com/en-us/library/windows/desktop/aa363908(v=vs.85).aspx
Defining an MS-DOS Device Name

Refer to http://msdn.microsoft.com/en-us/library/windows/hardware/ff554302(v=vs.85).aspx
Local and Global MS-DOS Device Names

mapNetDrive(remote, drive, user=None, password=None)
----------------------------------------------------

Create net drive from remote folder, and return the assigned drive
letter.

It uses the default user which initialize this remote connection if
user is None.

When drive is an empty string, the system will automatically assigns
network drive letters, letters are assigned beginning with Z:, then
Y:, and ending with C:\n.

For examples
  mapNetDrive(r'\\\\server\\data')
  mapNetDrive(r'\\\\server\\data', 'T:')
  mapNetDrive(r'\\\\server\\data', 'T:', r'\\\\server\\jack', 'abc')

Raise exception if something is wrong.

removeNetDrive(drive, force=True)
---------------------------------

Remove mapped drive specified by drive, For example,

  removeNetDrive('X:')

Parameter force specifies whether the disconnection should occur if
there are open files or jobs on the connection. If this parameter is
FALSE, the function fails if there are open files or jobs.

Raise exception if something is wrong, otherwise return None.

usageReport(drive)
------------------

Return a tuple to report the usage of the net drive:
  (available, total)

For examples,
  usageReport('Z:')

Raise exception if something is wrong.

userInfo()
----------

Get the logon user information, return a tuple:

  (server, domain, user)


Examples
========

It's easy to use, just like a common python library.

import netuse

# Get mapped net drive
print netuse.listNetDrive()

# Map a new drive, return the drive letter
print netuse.mapNetDrive(r'\\server\path')

# Remove a mapped drive
netuse.removeNetDrive('Z:')

# Get the usage of net drive: ( free bytes, total bytes ) 
print netuse.usageReport('Z:')
