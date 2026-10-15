@echo off
setlocal enabledelayedexpansion

rem явный путь (правь под себя), иначе ищем студию через vswhere
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
        if defined VSPATH set "VCVARS=!VSPATH!\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not exist "%VCVARS%" (
    echo [!] vcvars64.bat not found, install VS Build Tools or set VCVARS in build.bat
    exit /b 1
)
call "%VCVARS%" >nul

set "ROOT=%~dp0"
set "OUT=%ROOT%build"
set "OBJ=%ROOT%build\obj"
set "DLL=%ROOT%hookdll"
set "INJ=%ROOT%injector"
set "IMGUI=%ROOT%third_party\imgui"
set "MH=%ROOT%third_party\minhook"

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

echo === hookdll.dll ===
pushd "%OBJ%"
cl /nologo /MD /EHsc /O2 /std:c++17 /LD ^
   /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
   /I "%MH%\include" /I "%IMGUI%" /I "%IMGUI%\backends" ^
   "%DLL%\dllmain.cpp" ^
   "%DLL%\hookcore.cpp" ^
   "%DLL%\filters.cpp" ^
   "%DLL%\patches.cpp" ^
   "%DLL%\gui.cpp" ^
   "%IMGUI%\imgui.cpp" ^
   "%IMGUI%\imgui_draw.cpp" ^
   "%IMGUI%\imgui_tables.cpp" ^
   "%IMGUI%\imgui_widgets.cpp" ^
   "%IMGUI%\backends\imgui_impl_win32.cpp" ^
   "%IMGUI%\backends\imgui_impl_dx11.cpp" ^
   "%MH%\src\buffer.c" ^
   "%MH%\src\hook.c" ^
   "%MH%\src\trampoline.c" ^
   "%MH%\src\hde\hde64.c" ^
   /link /OUT:"%OUT%\hookdll.dll" /MACHINE:X64 ^
         d3d11.lib dxgi.lib user32.lib gdi32.lib
popd
if errorlevel 1 goto fail

echo === injector.exe ===
pushd "%OBJ%"
cl /nologo /MD /EHsc /O2 /std:c++17 ^
   /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN ^
   "%INJ%\main.cpp" ^
   /link /OUT:"%OUT%\injector.exe" /SUBSYSTEM:CONSOLE /MACHINE:X64 ^
         user32.lib advapi32.lib
popd
if errorlevel 1 goto fail

echo.
echo  Build OK:
echo    %OUT%\hookdll.dll
echo    %OUT%\injector.exe
exit /b 0

:fail
echo [!] BUILD FAILED
exit /b 1
