cd %~dp0..\

set BRAND=editbin /SUBSYSTEM:WINDOWS,5.00 /OSVERSION:5.0

%BRAND% out\final\x32\Release\task-homie\task-homie.exe
%BRAND% out\final\x32\Release\task-homie\task-homie-hook.dll
