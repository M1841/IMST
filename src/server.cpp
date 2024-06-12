#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

const size_t MAX_MESSAGE_SIZE = 4096;

enum
{
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2,
};

struct Connection
{
  int socket = -1;
  uint32_t state = 0;

  size_t read_buffer_size = 0;
  uint8_t read_buffer[4 + MAX_MESSAGE_SIZE];

  size_t write_buffer_size = 0;
  size_t write_buffer_sent = 0;
  uint8_t write_buffer[4 + MAX_MESSAGE_SIZE];
};

static void socket_set_nonblocking(int);

static void connection_put(std::vector<Connection *> &, struct Connection *);
static int32_t connection_accept(std::vector<Connection *> &, int);
static void connection_io(Connection *);

static void state_request(Connection *);
static void state_response(Connection *);

static bool try_fill_buffer(Connection *);
static bool try_flush_buffer(Connection *);
static bool try_one_request(Connection *);

static void exit_with_error(const char *);
static void print_error_message(const char *);

int main()
{
  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0)
  {
    exit_with_error("socket()");
  }

  int socket_option_value = 1;
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &socket_option_value, sizeof(socket_option_value));

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = ntohs(8080);
  server_addr.sin_addr.s_addr = ntohl(0);

  int bind_result = bind(server_socket, (const struct sockaddr *)&server_addr, sizeof(server_addr));
  if (bind_result)
  {
    exit_with_error("bind()");
  }

  int listen_result = listen(server_socket, SOMAXCONN);
  if (listen_result)
  {
    exit_with_error("listen()");
  }

  std::vector<Connection *> conns;

  socket_set_nonblocking(server_socket);

  std::vector<struct pollfd> poll_args;
  while (true)
  {
    poll_args.clear();

    struct pollfd server_poll = {server_socket, POLLIN, 0};
    poll_args.push_back(server_poll);

    for (Connection *conn : conns)
    {
      if (!conn)
      {
        continue;
      }

      struct pollfd client_poll = {};
      client_poll.fd = conn->socket;
      client_poll.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      client_poll.events |= POLLERR;

      poll_args.push_back(client_poll);
    }

    int poll_result = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if (poll_result < 0)
    {
      exit_with_error("poll");
    }

    for (size_t i = 1; i < poll_args.size(); ++i)
    {
      if (poll_args[i].revents)
      {
        Connection *conn = conns[poll_args[i].fd];
        connection_io(conn);

        if (conn->state == STATE_END)
        {
          conns[conn->socket] = NULL;
          (void)close(conn->socket);
          free(conn);
        }
      }
    }

    if (poll_args[0].revents)
    {
      (void)connection_accept(conns, server_socket);
    }
  }

  return 0;
}

static void socket_set_nonblocking(int socket)
{
  errno = 0;
  int flags = fcntl(socket, F_GETFL, 0);
  if (errno)
  {
    exit_with_error("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;

  errno = 0;
  (void)fcntl(socket, F_SETFL, flags);
  if (errno)
  {
    exit_with_error("fcntl error");
  }
}

static void connection_put(std::vector<Connection *> &conns, struct Connection *conn)
{
  if (conns.size() <= (size_t)conn->socket)
  {
    conns.resize(conn->socket + 1);
  }
  conns[conn->socket] = conn;
}

static int32_t connection_accept(std::vector<Connection *> &conns, int socket)
{
  struct sockaddr_in client_address = {};
  socklen_t client_address_length = sizeof(client_address);

  int client_socket = accept(socket, (struct sockaddr *)&client_address, &client_address_length);
  if (client_socket < 0)
  {
    print_error_message("accept() error");
    return -1;
  }

  socket_set_nonblocking(client_socket);

  struct Connection *conn = (struct Connection *)malloc(sizeof(struct Connection));
  if (!conn)
  {
    close(client_socket);
    return -1;
  }

  conn->socket = client_socket;
  conn->state = STATE_REQ;
  conn->read_buffer_size = 0;
  conn->write_buffer_size = 0;
  conn->write_buffer_sent = 0;
  connection_put(conns, conn);

  return 0;
}

static void connection_io(Connection *conn)
{
  if (conn->state == STATE_REQ)
  {
    state_request(conn);
  }
  else if (conn->state == STATE_RES)
  {
    state_response(conn);
  }
  else
  {
    assert(0);
  }
}

static void state_request(Connection *conn)
{
  while (try_fill_buffer(conn))
  {
  }
}

static void state_response(Connection *conn)
{
  while (try_flush_buffer(conn))
  {
  }
}

static bool try_fill_buffer(Connection *conn)
{
  assert(conn->read_buffer_size < sizeof(conn->read_buffer_size));

  ssize_t read_result = 0;
  do
  {
    size_t num_bytes = sizeof(conn->read_buffer) - conn->read_buffer_size;
    read_result = read(conn->socket, &conn->read_buffer[conn->read_buffer_size], num_bytes);
  } while (read_result < 0 && errno == EAGAIN);

  if (read_result < 0 && errno == EAGAIN)
  {
    return false;
  }
  if (read_result < 0)
  {
    print_error_message("read() error");
    conn->state = STATE_END;
    return false;
  }
  if (read_result == 0)
  {
    if (conn->read_buffer_size > 0)
    {
      print_error_message("unexpected EOF");
    }
    else
    {
      print_error_message("EOF");
    }

    conn->state = STATE_END;
    return false;
  }

  conn->read_buffer_size += (size_t)read_result;
  assert(conn->read_buffer_size <= sizeof(conn->read_buffer));

  while (try_one_request(conn))
  {
  }

  return (conn->state == STATE_REQ);
}

static bool try_flush_buffer(Connection *conn)
{
  ssize_t write_result = 0;
  do
  {
    size_t remaining_bytes = conn->write_buffer_size - conn->write_buffer_sent;
    write_result = write(conn->socket, &conn->write_buffer[conn->write_buffer_sent], remaining_bytes);
  } while (write_result < 0 && errno == EINTR);

  if (write_result < 0 && errno == EAGAIN)
  {
    return false;
  }
  if (write_result < 0)
  {
    print_error_message("write() error");
    conn->state = STATE_END;
    return false;
  }

  conn->write_buffer_sent += (size_t)write_result;
  assert(conn->write_buffer_sent <= conn->write_buffer_size);

  if (conn->write_buffer_sent == conn->write_buffer_size)
  {
    conn->state = STATE_REQ;
    conn->write_buffer_sent = 0;
    conn->write_buffer_size = 0;
    return false;
  }
  return true;
}

static bool try_one_request(Connection *conn)
{
  if (conn->read_buffer_size < 4)
  {
    return false;
  }

  uint32_t message_length = 0;
  memcpy(&message_length, &conn->read_buffer[0], 4);

  if (message_length > MAX_MESSAGE_SIZE)
  {
    print_error_message("too long");
    conn->state = STATE_END;
    return false;
  }
  if (4 + message_length > conn->read_buffer_size)
  {
    return false;
  }

  printf("client says: %.*s\n", message_length, &conn->read_buffer[4]);

  memcpy(&conn->write_buffer[0], &message_length, 4);
  memcpy(&conn->write_buffer[4], &conn->read_buffer[4], message_length);
  conn->write_buffer_size = 4 + message_length;

  size_t remaining_bytes = conn->read_buffer_size - 4 - message_length;
  if (remaining_bytes > 0)
  {
    memmove(conn->read_buffer, &conn->read_buffer[4 + message_length], remaining_bytes);
  }
  conn->read_buffer_size = remaining_bytes;

  conn->state = STATE_RES;
  state_response(conn);

  return (conn->state == STATE_REQ);
}

static void exit_with_error(const char *message)
{
  int error_code = errno;
  fprintf(stderr, "[%d] %s\n", error_code, message);
  abort();
}

static void print_error_message(const char *message)
{
  fprintf(stderr, "%s\n", message);
}
