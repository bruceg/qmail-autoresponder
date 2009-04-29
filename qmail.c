#include <sysdeps.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qmail-autoresponder.h"

static pid_t inject_pid;
static pid_t queue_pid;
static int msgfd;
static int envfd;

static void exec_qmail(const char *cmd, const char* arg1,
		       int fd0, int fd1)
{
  const char* argv[] = { cmd, arg1, 0 };
  dup2(fd0, 0);
  dup2(fd1, 1);
  close(fd0);
  close(fd1);
  execv(cmd, (char**)argv);
  fail_temp("Could not exec qmail");
}

static void exec_qmail_inject(int fd0, int fd1)
{
  exec_qmail("/var/qmail/bin/qmail-inject", "-n", fd0, fd1);
}

static void exec_qmail_queue(int fd0, int fd1)
{
  exec_qmail("/var/qmail/bin/qmail-queue", 0, fd0, fd1);
}

int qmail_start(void)
{
  int pipe1[2];			/* Pipe into -inject for the message body */
  int pipe2[2];			/* Pipe from -inject into -queue */
  int pipe3[2];			/* Pipe into -queue for the envelope */

  if (pipe(pipe1) == -1
      || pipe(pipe2) == -1
      || pipe(pipe3) == -1)
    fail_temp("Could not create pipe");

  inject_pid = fork();
  switch (inject_pid) {
  case -1:
    fail_temp("Could not fork");
    break;
  case 0:
    close(pipe1[1]);
    close(pipe2[0]);
    close(pipe3[0]);
    close(pipe3[1]);
    /* Read from pipe1 and write to pipe2 */
    exec_qmail_inject(pipe1[0], pipe2[1]);
    break;
  }
  close(pipe1[0]);
  close(pipe2[1]);
  msgfd = pipe1[1];

  queue_pid = fork();
  switch (queue_pid) {
  case -1:
    fail_temp("Could not fork");
    break;
  case 0:
    close(pipe1[1]);
    close(pipe2[1]);
    close(pipe3[1]);
    /* Read message from pipe2 and envelope from pipe3 */
    exec_qmail_queue(pipe2[0], pipe3[0]);
    break;
  }
  close(pipe2[0]);
  close(pipe3[0]);
  envfd = pipe3[1];

  return msgfd;
}

void qmail_finish(const char* sender)
{
  int status;
  const size_t senderlen = strlen(sender);
  char envelope[5 + senderlen];

  close(msgfd);
  if(waitpid(inject_pid, &status, WUNTRACED) == -1)
    fail_temp("Failed to catch exit status of qmail-inject");
  if(!WIFEXITED(status))
    fail_temp("qmail-inject crashed");
  if(WEXITSTATUS(status))
    fail_temp("qmail-inject failed");

  memcpy(envelope, "F\0T", 3);
  memcpy(envelope+3, sender, senderlen+1);
  envelope[3+senderlen+1] = 0;

  write(envfd, envelope, sizeof envelope);
  close(envfd);

  if(waitpid(queue_pid, &status, WUNTRACED) == -1)
    fail_temp("Failed to catch exit status of qmail-queue");
  if(!WIFEXITED(status))
    fail_temp("qmail-queue crashed");
  if(WEXITSTATUS(status))
    fail_temp("qmail-queue failed");

  fprintf(stderr, "%s: Sent response qp %d\n", argv0, queue_pid);
}
