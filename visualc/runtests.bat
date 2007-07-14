SETLOCAL

set PATH=%CD%\visualc\Release;%PATH%

unit_tests.exe
if %ERRORLEVEL%=="0" goto tester-testsuite.lua
@echo unit-tests failed, skipping remaining tests.
goto error

:tester-testsuite.lua
tester.exe tester-testsuite.lua
if NOT %ERRORLEVEL%=="0" goto testsuite.lua
@echo tester-testsuite.lua failed, skipping remaining tests.
goto error

:testsuite.lua
tester.exe testsuite.lua
if NOT %ERRORLEVEL%=="0" goto end
@echo testsuite.lua failed.
goto error



goto end
:error
@rem return an error code
exit /B 1



:end
