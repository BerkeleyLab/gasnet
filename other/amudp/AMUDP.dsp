# Microsoft Developer Studio Project File - Name="AMUDP" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=AMUDP - Win32 UFXP Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "AMUDP.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "AMUDP.mak" CFG="AMUDP - Win32 UFXP Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "AMUDP - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "AMUDP - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE "AMUDP - Win32 UFXP Debug" (based on "Win32 (x86) Console Application")
!MESSAGE "AMUDP - Win32 UFXP Release" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""$/Ti/AMUDP", GVBAAAAA"
# PROP Scc_LocalPath "."
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "AMUDP - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /W3 /GX /O2 /I "." /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386

!ELSEIF  "$(CFG)" == "AMUDP - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /I "." /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "DEBUG" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ELSEIF  "$(CFG)" == "AMUDP - Win32 UFXP Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "AMUDP___Win32_UFXP_Debug"
# PROP BASE Intermediate_Dir "AMUDP___Win32_UFXP_Debug"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "UFXPDebug"
# PROP Intermediate_Dir "UFXPDebug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /I "." /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "DEBUG" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /I "." /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "DEBUG" /D "UFXP" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ELSEIF  "$(CFG)" == "AMUDP - Win32 UFXP Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "AMUDP___Win32_UFXP_Release"
# PROP BASE Intermediate_Dir "AMUDP___Win32_UFXP_Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "UFXPRelease"
# PROP Intermediate_Dir "UFXPRelease"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /W3 /GX /O2 /I "." /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /D "UFXP" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386

!ENDIF 

# Begin Target

# Name "AMUDP - Win32 Release"
# Name "AMUDP - Win32 Debug"
# Name "AMUDP - Win32 UFXP Debug"
# Name "AMUDP - Win32 UFXP Release"
# Begin Group "Header Files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\amudp.h
# End Source File
# Begin Source File

SOURCE=.\amudp_internal.h
# End Source File
# Begin Source File

SOURCE=.\amudp_spmd.h
# End Source File
# Begin Source File

SOURCE=.\ueth.h
# End Source File
# End Group
# Begin Group "Source Files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\amudp_ep.cpp
# End Source File
# Begin Source File

SOURCE=.\amudp_reqrep.cpp
# End Source File
# Begin Source File

SOURCE=.\amudp_spawn.cpp
# End Source File
# Begin Source File

SOURCE=.\amudp_spmd.cpp
# End Source File
# End Group
# Begin Group "Utils"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\exc.cpp
# End Source File
# Begin Source File

SOURCE=.\exc.h
# End Source File
# Begin Source File

SOURCE=.\sig.cpp
# End Source File
# Begin Source File

SOURCE=.\sig.h
# End Source File
# Begin Source File

SOURCE=.\sockaddr.h
# End Source File
# Begin Source File

SOURCE=.\socket.h
# End Source File
# Begin Source File

SOURCE=.\socklist.cpp
# End Source File
# Begin Source File

SOURCE=.\socklist.h
# End Source File
# Begin Source File

SOURCE=.\sockutil.cpp
# End Source File
# Begin Source File

SOURCE=.\sockutil.h
# End Source File
# End Group
# Begin Group "Configuration"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Makefile
# End Source File
# Begin Source File

SOURCE=.\Makefile.aix.rs6000
# End Source File
# Begin Source File

SOURCE=.\Makefile.common
# End Source File
# Begin Source File

SOURCE=.\Makefile.linux.i386
# End Source File
# Begin Source File

SOURCE=.\Makefile.linux.i486
# End Source File
# Begin Source File

SOURCE=.\Makefile.posix.i386
# End Source File
# Begin Source File

SOURCE=.\Makefile.solaris.i386
# End Source File
# Begin Source File

SOURCE=.\Makefile.solaris.sparc
# End Source File
# Begin Source File

SOURCE=.\Makefile.titanium.in
# End Source File
# Begin Source File

SOURCE=.\Makefile.unicosmk.cray_t3e
# End Source File
# End Group
# Begin Group "Applications"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\apputils.c
# End Source File
# Begin Source File

SOURCE=.\apputils.h
# End Source File
# Begin Source File

SOURCE=.\testamudp.c
# End Source File
# Begin Source File

SOURCE=.\testbounce.c

!IF  "$(CFG)" == "AMUDP - Win32 Release"

!ELSEIF  "$(CFG)" == "AMUDP - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "AMUDP - Win32 UFXP Debug"

!ELSEIF  "$(CFG)" == "AMUDP - Win32 UFXP Release"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\testbulk.c

!IF  "$(CFG)" == "AMUDP - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "AMUDP - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "AMUDP - Win32 UFXP Debug"

!ELSEIF  "$(CFG)" == "AMUDP - Win32 UFXP Release"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\testgetput.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\testlatency.c
# End Source File
# Begin Source File

SOURCE=.\testping.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\testreadwrite.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\testreduce.c

!IF  "$(CFG)" == "AMUDP - Win32 Release"

!ELSEIF  "$(CFG)" == "AMUDP - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "AMUDP - Win32 UFXP Debug"

!ELSEIF  "$(CFG)" == "AMUDP - Win32 UFXP Release"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# End Group
# End Target
# End Project
