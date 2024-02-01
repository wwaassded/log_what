#include <cassert>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

namespace what::Log {

#define THREADNAME_WIDTH 16
#define FILENAME_WIDTH 23
#define PRIFIX_WIDTH (53 + THREADNAME_WIDTH + FILENAME_WIDTH)

#define ASSERT(predict, str)                                                   \
  do {                                                                         \
    if (!(predict)) {                                                          \
      fprintf(stderr, "%s\n", (str));                                          \
      assert((predict));                                                       \
    }                                                                          \
  } while (0);

#define VLOG(verbosity, ...)                                                   \
  what::Log::log(verbosity, __FILE__, __LINE__, __VA_ARGS__);

// LOG(INFO,"test:%s\n",str)
#define LOG(verbosityname, ...)                                                \
  VLOG(what::Log::Verbosity::Verbosity##verbosityname, __VA_ARGS__)

enum class Verbosity {
  VerbosityFATAL = -3,
  VerbosityERROR,
  VerbosityWARNING,
  VerbosityINFO,
  VerbosityMESSAGE,
};

auto get_verbosity_name(Verbosity verbosity) -> char *;

pthread_key_t thread_key;   // "static" thread local
pthread_once_t thread_once; // call only once

void init_thread_key() { pthread_key_create(&thread_key, free); }

void Set_thread_name(const char *str);

class Text {
public:
  explicit Text(char *str) : __str(str) {}

  //* 禁止拷贝
  Text(const Text &) = delete;
  auto operator=(const Text &) -> Text & = delete;

  Text(Text &&other);

  ~Text();

  auto Release() -> char *;

  auto C_str() -> const char * { return __str; }

  inline auto Is_empty() const -> bool {
    return __str == nullptr || __str[0] == '\0';
  }

private:
  char *__str{nullptr}; //初始化为nullptr
};

class Message {
public:
  /* already in prifix*/
  Verbosity verbosity;
  const char *file;
  unsigned int line;
  const char *prifix;
  const char *raw_message;
};

void log(Verbosity verbosity, const char *file, unsigned int line,
         const char *format, ...);

void log_to_everywhere(Verbosity verbosity, const char *file, unsigned line,
                       const char *message);

void log_message(Verbosity verbosity, Message &message);

auto vastextprint(const char *format, va_list list) -> Text;

void print_prifix(char *prefix, size_t prefix_len, Verbosity verbosity,
                  const char *file, unsigned int line);

void get_thread_name(char *thread_name, size_t thread_name_len);

auto filename(const char *) -> const char *;

} // namespace what::Log