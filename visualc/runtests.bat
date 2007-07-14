SETLOCAL

set PATH=%CD%\visualc\Release;%PATH%

unit_tests.exe
if %ERRORLEVEL%=="0" goto tester-testsuite.lua
@echo unit-tests failed, skipping remaining tests.
goto end

:tester-testsuite.lua
tester.exe tester-testsuite.lua
if %ERRORLEVEL%=="0" goto testsuite.lua
@echo tester-testsuite.lua failed, skipping remaining tests.
goto end

:testsuite.lua
tester.exe testsuite.lua
if %ERRORLEVEL%=="0" goto end
@echo testsuite.lua failed.
goto end





:end
