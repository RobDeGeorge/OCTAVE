/* The console's commands. */
#ifndef CMDS_H
#define CMDS_H

/* Runs one line (it is cut into words in place). Prints its own errors. */
void cmds_run(char *line);

/* called from the main loop: lets an armed bus disarm itself after two minutes */
void cmds_poll(void);

#endif /* CMDS_H */
