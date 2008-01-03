SETLOCAL

if "%1x"=="debugx" goto do_debug
set EXE_PATH=visualc\Release
goto setpath

:do_debug
set EXE_PATH=visualc\Debug
goto setpath

:setpath
set PATH=%CD%\%EXE_PATH%;%PATH%

set fail_count=0

%EXE_PATH%\tester.exe tester-testsuite.lua
if %ERRORLEVEL%==0 goto unit-testsuite.lua
@echo tester-testsuite.lua failed.
set fail_count=1

:unit-testsuite.lua
%EXE_PATH%\tester.exe unit-testsuite.lua
if %ERRORLEVEL%==0 goto lua-testsuite.lua
@echo unit-testsuite.lua failed.
if %fail_count%==1 set fail_count=2
if %fail_count%==0 set fail_count=1

:lua-testsuite.lua
%EXE_PATH%\tester.exe lua-testsuite.lua
if %ERRORLEVEL%==0 goto nomore
@echo lua-testsuite.lua failed.
if %fail_count%==2 set fail_count=3
if %fail_count%==1 set fail_count=2
if %fail_count%==0 set fail_count=1

:nomore
if %fail_count%==0 goto end

@rem return an error code
@echo ===============================
@echo %fail_count% of 3 tests failed.
@echo ===============================
exit /B 1

:end
@echo ===============================
@echo All tests succeeded!
@echo ===============================
