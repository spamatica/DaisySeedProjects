@echo off
rem Starts the pedal setup tool (tools\effects.py) on Windows. Arguments are passed on to it.
rem Started from Git BASH (mintty) without arguments, it opens a real console window for the full-screen tool.
rem First run: creates a virtual environment in .venv and installs windows-curses into it (needs Python 3.13 from python.org).
setlocal
cd /d "%~dp0"

rem Git BASH sets MSYSTEM; the full-screen tool does not work in its terminal, so run in a new console window.
if not defined MSYSTEM goto start_here
if not "%~1"=="" goto start_here
set "MSYSTEM="
set "PEDAL_NEWWIN=1"
start "Pedal setup" /wait cmd /c ""%~f0""
exit /b %errorlevel%

:start_here
set "VENV_PY=%~dp0.venv\Scripts\python.exe"
if exist "%VENV_PY%" goto have_venv

set "BASE_PY="
where py >nul 2>nul
if errorlevel 1 goto try_python313
py -3.13 -c "import sys" >nul 2>nul
if errorlevel 1 goto try_python313
set "BASE_PY=py -3.13"
goto make_venv

:try_python313
where python3.13 >nul 2>nul
if errorlevel 1 goto no_python
set "BASE_PY=python3.13"
goto make_venv

:no_python
echo Python 3.13 was not found. Install it from https://www.python.org/downloads/ (not the Microsoft Store version)
echo and run this file again.
goto fail

:make_venv
echo Creating the virtual environment in .venv ...
%BASE_PY% -m venv .venv
if errorlevel 1 goto venv_failed
"%VENV_PY%" -m pip install windows-curses
if errorlevel 1 goto curses_failed
goto have_venv

:venv_failed
echo Could not create the virtual environment.
goto fail

:curses_failed
echo Could not install windows-curses.
rmdir /s /q .venv
goto fail

:have_venv
rem Git's sh.exe is needed by NAM setup (update_models.sh): add Git for Windows' usr\bin to PATH for this session.
where sh >nul 2>nul
if not errorlevel 1 goto run
if exist "%ProgramFiles%\Git\usr\bin\sh.exe" set "PATH=%ProgramFiles%\Git\usr\bin;%PATH%"
if exist "%LocalAppData%\Programs\Git\usr\bin\sh.exe" set "PATH=%LocalAppData%\Programs\Git\usr\bin;%PATH%"

:run
"%VENV_PY%" tools\effects.py %*
if errorlevel 1 if defined PEDAL_NEWWIN pause
exit /b %errorlevel%

:fail
rem In a window opened by this script, keep it open so the message can be read.
if defined PEDAL_NEWWIN pause
exit /b 1
