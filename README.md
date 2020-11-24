# small_shell
• Created a C-based shell application that emulates bash shell; supporting built in commands.

• Utilizes POSIX signals with changes in signal behaviors via signal handlers.

• Applies I/O redirection and executed commands fork child processes that gets reaped properly.

Please compile using the following gcc command arguments:

gcc -std=gnu99 -o smallsh smallsh.c
