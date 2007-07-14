SETLOCAL

set PATH=%CD%\visualc\Release;%PATH%
unit_tests.exe
tester.exe tester-testsuite.log
tester.exe testsuite.lua

