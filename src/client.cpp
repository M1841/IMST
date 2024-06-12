#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

const size_t MAX_MESSAGE_SIZE = 4096;

static int32_t send_request(int, const char *);
static int32_t read_response(int);

static int32_t read_full_message(int, char *, size_t);
static int32_t write_full_message(int, const char *, size_t);

static void exit_with_error(const char *);
static void print_error_message(const char *);

int main()
{
  int client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket < 0)
  {
    exit_with_error("socket()");
  }

  struct sockaddr_in server_address = {};
  server_address.sin_family = AF_INET;
  server_address.sin_port = ntohs(8080);
  server_address.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);

  int connection_status = connect(client_socket, (const struct sockaddr *)&server_address, sizeof(server_address));
  if (connection_status)
  {
    exit_with_error("connect()");
  }

  const char *query_list[3] = {"hello1", "hello2", "hello3"};
  for (size_t i = 0; i < 3; ++i)
  {
    int32_t error = send_request(client_socket, query_list[i]);
    if (error)
    {
      close(client_socket);
      return 0;
    }
  }
  for (size_t i = 0; i < 3; ++i)
  {
    int32_t error = read_response(client_socket);
    if (error)
    {
      close(client_socket);
      return 0;
    }
  }
}

static int32_t send_request(int socket, const char *message)
{
  uint32_t message_length = (uint32_t)strlen(message);
  if (message_length > MAX_MESSAGE_SIZE)
  {
    return -1;
  }

  char write_buffer[4 + MAX_MESSAGE_SIZE];
  memcpy(write_buffer, &message_length, 4);
  memcpy(&write_buffer[4], message, message_length);

  int32_t error_code = write_full_message(socket, write_buffer, 4 + message_length);
  if (error_code)
  {
    return error_code;
  }

  return 0;
}

static int32_t read_response(int socket)
{
  char read_buffer[4 + MAX_MESSAGE_SIZE + 1];
  errno = 0;

  int32_t error_code = read_full_message(socket, read_buffer, 4);
  if (error_code)
  {
    print_error_message(errno ? "read() error" : "EOF");
    return error_code;
  }

  uint32_t message_length = 0;
  memcpy(&message_length, read_buffer, 4);
  if (message_length > MAX_MESSAGE_SIZE)
  {
    print_error_message("too long");
    return -1;
  }

  error_code = read_full_message(socket, &read_buffer[4], message_length);
  if (error_code)
  {
    print_error_message("read() error");
    return error_code;
  }

  read_buffer[4 + message_length] = '\0';
  printf("server says: %s\n", &read_buffer[4]);
  return 0;
}

static int32_t read_full_message(int socket, char *buffer, size_t num_bytes)
{
  while (num_bytes > 0)
  {
    ssize_t num_bytes_read = read(socket, buffer, num_bytes);
    if (num_bytes_read <= 0)
    {
      return -1;
    }
    assert((size_t)num_bytes_read <= num_bytes);
    num_bytes -= (size_t)num_bytes_read;
    buffer += num_bytes_read;
  }
  return 0;
}

static int32_t write_full_message(int socket, const char *buffer, size_t num_bytes)
{
  while (num_bytes > 0)
  {
    ssize_t num_bytes_written = write(socket, buffer, num_bytes);
    if (num_bytes_written <= 0)
    {
      return -1;
    }
    assert((size_t)num_bytes_written <= num_bytes);
    num_bytes -= (size_t)num_bytes_written;
    buffer += num_bytes_written;
  }
  return 0;
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
