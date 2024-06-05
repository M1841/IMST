#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void do_something(int);
static void die(const char *);
static void msg(const char *);

int main()
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    die("socket()");
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(8080);
  addr.sin_addr.s_addr = ntohl(0);

  int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv)
  {
    die("bind()");
  }

  rv = listen(fd, SOMAXCONN);
  if (rv)
  {
    die("listen()");
  }

  while (true)
  {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);

    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0)
    {
      continue;
    }

    do_something(connfd);
    close(connfd);
  }

  return 0;
}

static void do_something(int connfd)
{
  char rbuf[64] = {};

  ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
  if (n < 0)
  {
    msg("read() error");
    return;
  }
  printf("client says: %s\n", rbuf);

  char wbuf[] = "world";
  write(connfd, wbuf, strlen(wbuf));
}

static void die(const char *str)
{
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, str);
  abort();
}

static void msg(const char *str)
{
  fprintf(stderr, "%s\n", str);
}