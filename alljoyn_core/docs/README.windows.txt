
AllJoyn SDK for Windows 7
-------------------------

This subtree contains one complete copy of the AllJoyn SDK for Windows 7, built
for a single CPU type (x86_64), VARIANT (either debug or release),
and for a single version of Microsoft Visual Studio (2013).

The CPU, VARIANT, and MSVS version is normally incorporated into the name of the
package or folder containing this SDK.

NOTE:

    This SDK works with the Microsoft Win32 architecture, used in
    traditional Microsoft Windows desktops and servers with x86_64 CPUs.

    This SDK does NOT work with the Microsoft WinRT architecture.

Windows, Win32, WinRT, and Visual Studio are registered trademarks or trademarks
of the Microsoft group of companies.


Please see ReleaseNotes.txt for the applicable AllJoyn release version and
related information on new features and known issues.


Summary of file and directory structure:
----------------------------------------

The contents of this SDK are arranged into the following top level folders:

cpp/    core AllJoyn functionality, implemented in C++
          - built from alljoyn.git source subtrees alljoyn_core and common
          - required for all AllJoyn applications
java/   optional Java language binding          (built from alljoyn_java)
js/     optional Javascript binding             (built from alljoyn_js)
c/      optional ANSI C language binding        (built from alljoyn_c)


The contents of each top level folder are further arranged into sub-folders:

        ----------------------------------------------
cpp/    core AllJoyn functionality, implemented in C++
        ----------------------------------------------

    bin/                        executable binaries
                                  - as of 3.4.0, several test programs are
                                    intentionally removed from published SDK's

    bin/samples/                pre-built sample programs

        SampleDaemon                    Easy-to-use daemon for use with
                                        AllJoyn Thin Client

    docs/html/                  AllJoyn Core API documentation

    inc/alljoyn/                AllJoyn Core (C++) headers
    inc/qcc/

    lib/                        AllJoyn Core (C++) client libraries

        alljoyn.lib                     implements core API
        ajrouter.lib                    implements bundled-router
        daemonlib.lib

    samples/                    C++ sample programs (see README)


        ---------------------
java/   Java language binding
        ---------------------

    docs/html/                  API documentation

    jar/                        client library, misc jar files

        alljoyn.jar                     AllJoyn Java API

    lib/alljoyn_java.dll        client libraries
    lib/alljoyn_java.exp
    lib/alljoyn_java.lib

    samples/                    sample programs (see README)


        ---------------------------
js/     Javascript language binding
        ---------------------------

    docs/html/                  API documentation

    lib/npalljoyn.dll           client libraries
    lib/npalljoyn.exp
    lib/npalljoyn.lib

    plugin/npalljoyn.dll        Installable Netscape plugin for AllJoyn

    samples/                    sample Javascript apps


        -----------------------
c/      ANSI C language binding
        -----------------------

    docs/html/                  API documentation

    inc/alljoyn_c/              ANSI C headers
    inc/qcc/

    lib/                        client libraries


----------------------------------------------------------------------------------
Running and developing AllJoyn 14.06 applications on Windows 10 Technical Preview 
(released 10/21/2014)
----------------------------------------------------------------------------------

Windows 10 includes native support for AllJoyn 14.06. In the Windows 10 Technical 
Preview build, you can use the built-in AllJoyn router node service which means 
that your desktop applications don't need to bundle an AllJoyn router node, and 
you don't need to run a stand-alone router node application in order to run AllJoyn 
desktop applications.
 
In Windows 10 Technical Preview builds, the AllJoyn router node service 
(AJRouter.dll) must be started manually as follows from an elevated command prompt:
    net start ajrouter
 
If you need to stop the router node service, you can either reboot your PC, or 
execute the following command from an elevated command prompt:
    net stop ajrouter
 
More information about AllJoyn integration in Windows will be available in future 
releases of the AllSeen SDK for Windows.
