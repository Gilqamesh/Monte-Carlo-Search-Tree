@echo off

set GenerateIntrinsicFunctions=/Oi
set MaximizeSpeed=/O2
set FullPathInDiagnostics=/FC
set EnhanceOptimizedDebugging=/Zo
set FullSmbolicDebuggingInformation=/Z7
set NoLogo=/nologo
set DisableStandardCStackUnwinding=/EHa-
set EnableStandardCStackUnwinding=/EHa
set MultithreadStaticRunTimeLibraryWithDebug=/MTd
set MultithreadStaticRunTimeLibrary=/MT
set MultithreadDLLRunTimeLibraryWithDebug=/MDd
set MultithreadDLLRunTimeLibrary=/MD
set CreateDLLWithDebug=/LDd
set CreateDLL=/LD
set CVersion=/std:c++17

set DebugCompilerFlags=%GenerateIntrinsicFunctions%^
                       %EnhanceOptimizedDebugging%^
                       %FullSmbolicDebuggingInformation%^
                       %NoLogo%^
                       %EnableStandardCStackUnwinding%^
                       %MultithreadStaticRunTimeLibraryWithDebug%^
                       %CVersion%

set NoIncrementalLinking=/INCREMENTAL:NO
set IncrementalLinking=/INCREMENTAL
set EliminateNotReferencedFunctions=/OPT:REF
set KeepNotReferencedFunctions=/OPT:NOREF
set ConsoleApplication=/SUBSYSTEM:CONSOLE

set LinkerFlags=%NoIncrementalLinking%^
                %ConsoleApplication%^
                %EliminateNotReferencedFunctions%

pushd build
cl %DebugCompilerFlags% ../src/main.cpp /link %LinkerFlags%
popd

REM /Oi Generate Intrinsic Functions

REM /O2 Maximize Speed

REM /FC Full path of source code file in diagnostics (__FILE__ for example)

REM /Zo Enhance Optimized Debugging (can debug optimized code)

REM /Z7 full symbolic debugging information in obj files

REM /nologo Suppress Startup Banner

REM /EHa- disables standard C++ stack unwinding

REM /MTd multithread, static version of the run-time library, defines _DEBUG and _MT and
REM causes the compiler to place the library name LIBCMT.lib into the .obj file so that
REM the linker will use LIBCMT.lib to resolve external symbols

REM /MDd multithread, dll-specific version of the run-time librar, defines _DEBUG, _MT and
REM _DLL and causes the compiler to place the library named MSVCRTD.lib into the .obj file

REM /LDd creates a dll, implies /MT, unless explicitly /MD is specified, also defines _MT and _DEBUG

REM /INCREMENTAL:NO disables incremental linking, incremental linking speeds up dev process,
REM as linking doesn't have to be from scratch every time, but makes the dll .exe/.dll larger
REM and slower, and also is incompatible with link time code generation, losing performance opt

REM /OPT:REF Eliminates functions and data that are never referenced, the linker removes
REM unreferenced packaged fns and data, known as COMDATs, also disables incremental linking

REM /SUBSYSTEM:WINDOWS the application doesn't require a console, probably because it
REM creates its own windows for interaction with the user, WinMain has to be defined

REM /SUBSYSTEM:CONSOLE console application, define main for native code

REM /PDB:filename program database, which holds debugging information
