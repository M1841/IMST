#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

const size_t MAX_MESSAGE_SIZE = 4096;

static int32_t handle_single_request(int);

static int32_t read_full_message(int, char *, size_t);
static int32_t write_full_message(int, const char *, size_t);

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

  while (true)
  {
    struct sockaddr_in client_addr = {};
    socklen_t client_addr_length = sizeof(client_addr);

    int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_length);
    if (client_socket < 0)
    {
      continue;
    }

    while (true)
    {
      int32_t error_code = handle_single_request(client_socket);
      if (error_code)
      {
        break;
      }
    }
    close(client_socket);
  }

  return 0;
}

static int32_t handle_single_request(int client_socket)
{
  char read_buffer[4 + MAX_MESSAGE_SIZE + 1];
  errno = 0;

  int32_t error_code = read_full_message(client_socket, read_buffer, 4);
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

  error_code = read_full_message(client_socket, &read_buffer[4], message_length);
  if (error_code)
  {
    print_error_message("read() error");
    return error_code;
  }

  read_buffer[4 + message_length] = '\0';
  printf("client says: %s\n", &read_buffer[4]);

  const char reply[] = "world!";
  char write_buffer[4 + sizeof(reply)];
  message_length = (uint32_t)strlen(reply);

  memcpy(write_buffer, &message_length, 4);
  memcpy(&write_buffer[4], reply, message_length);

  return write_full_message(client_socket, write_buffer, 4 + message_length);
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
