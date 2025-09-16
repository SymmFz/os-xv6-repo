#include "kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]) {
  if (argc != 1) {
    printf("Usage: pingpong\n");
    exit(1);
  }

  int ctf[2], ftc[2];
  if (pipe(ctf) < 0 || pipe(ftc) < 0) {
    fprintf(2, "pingpong: pipe() failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "pingpong: fork() failed\n");
    exit(1);
  }
  
  if (pid == 0) {
    // child process
    close(ctf[0]);
    close(ftc[1]);

    int child_pid = getpid();
    int father_pid;
    char buf[512];

    // child receive pid from father
    if (read(ftc[0], buf, sizeof(buf)) > 0) {
      father_pid = atoi(buf);
      printf("%d: received ping from pid %d\n", child_pid, father_pid);
    } else {
      fprintf(2, "pingpong: child read failed\n");
      exit(1);
    }

    // child send pid pid to father
    itoa(child_pid, buf);
    if (write(ctf[1], buf, strlen(buf) + 1) != strlen(buf) + 1) {
      fprintf(2, "pingpong: child write failed\n");
      exit(1);
    }

    close(ftc[0]);
    close(ctf[1]);
    exit(0);

  } else {
    close(ftc[0]);
    close(ctf[1]);

    int child_pid;
    int father_pid = getpid();
    char buf[512];

    // father send pid to child
    itoa(father_pid, buf);
    if (write(ftc[1], buf, strlen(buf) + 1) != strlen(buf) + 1) {
      fprintf(2, "pingpong: father write failed\n");
      exit(1);
    }

    // father recevie pid from child
    if (read(ctf[0], buf, sizeof(buf)) > 0) {
      child_pid = atoi(buf);
      printf("%d: received pong from pid %d\n", father_pid, child_pid);
    } else {
      fprintf(2, "pingpong: father read failed\n");
      exit(1);
    }

    close(ftc[1]);
    close(ctf[0]);
    wait(0);
    exit(0);
  }
}

