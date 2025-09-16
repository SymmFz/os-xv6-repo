#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fs.h"

char *getname(char *path) {
  char *p;
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  return p;
}


void find(char *path, char *name) {
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    printf("find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    printf("find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if (strcmp(getname(path), name) == 0) {
    printf("%s\n", path);
  }

  if (st.type == T_DIR) {
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
      printf("find: path too long\n");
      return;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;
      if (de.inum == 0) continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if (stat(buf, &st) < 0) {
        printf("find: cannot stat %s\n", buf);
        continue;
      }
      find(buf, name);
    }
  }
  close(fd);
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf("Usage: find <PATH> <NAME>\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}

