task-homie is a tiny Windows applet that completely hides auto-hidden
taskbars, such that the edge of the screen containing the taskbar is no longer
a giant unusable ~ danger zone ~.
  __
 /..\
 \_O/

Usage:
Acquire win2k+. Run task-homie.exe. Aggressively smash the left or right
windows key to summon the taskbar and start menu.

Build:
Get a recent copy of premake 4 and a copy of Visual Studio 2013. Punch your
keyboard until an executable comes out.

Known Issues:
task-homie leaks two USER handles every time it receives a "TaskbarCreated"
broadcast message if the hooking target thread no longer exists (this can
happen if explorer crashes/is terminated and is later restarted). When this
happens, the "UnhookWindowsHookEx" call fails without modifying the last-error
code.  As a workaround, task-homie will restart on receipt of a
"TaskbarCreated" message.

C++?
:(
I regret having contributed to the existence of more C++ software (I initially
wrote this in F#), but starting the entire .NET RTS in the hook target process
for a hook as trivial as this is overkill!
