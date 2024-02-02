#ifndef LOG_WHAT_HPP
#define LOG_WHAT_HPP

#include <cassert>
#define TERMINAL_HAS_COLOR 1
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>
#include <vector>

// TODO: add_call_back(), file's callback, handle_fatal(), backtrace()

namespace what::Log {

enum class Verbosity {
  VerbosityFATAL = -3,
  VerbosityERROR = -2,
  VerbosityWARNING = -1,
  VerbosityINFO = 0,
  VerbosityMESSAGE = 1, //普通的信息
};

enum class FileMode { Truncate, Append };

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
  enum Verbosity verbosity;

  const char *file;

  unsigned int line;

  const char *prefix;

  const char *raw_message;
};

typedef void (*call_back_handler_t)(void *user_data, Message &);
typedef void (*flush_handler_t)(void *user_data);
typedef void (*close_handler_t)(void *user_data);

class CallBack {
public:
  void *user_data;

  call_back_handler_t call_back;

  flush_handler_t flush;

  close_handler_t close;

  Verbosity max_verbosit;
};

#define THREADNAME_WIDTH 16
#define FILENAME_WIDTH 23
#define PREFIX_WIDTH (53 + THREADNAME_WIDTH + FILENAME_WIDTH)

//* VT100 control your terminal
#ifdef TERMINAL_HAS_COLOR
//! make sure that your terminal has color
#define VTSEQ(ID) ("\033[" #ID "m")
#else
#define VTSEQ(ID) ""
#endif

#define TERMINAL_RED VTSEQ(31)
#define TERMINAL_GREEN VTSEQ(32)
#define TERMINAL_YELLOW VTSEQ(33)
#define TERMINAL_DIM VTSEQ(2)
//! start and end with it everytime
#define TERMINAL_RESET VTSEQ(0)

#define ASSERT(predict, str)                                                   \
  do {                                                                         \
    if (!(predict)) {                                                          \
      fprintf(stderr, "%s\n", (str));                                          \
      assert((predict));                                                       \
    }                                                                          \
  } while (0);

void log(Verbosity verbosity, const char *file, unsigned int line,
         const char *format, ...);

#define VLOG(verbosity, ...)                                                   \
  what::Log::log(verbosity, __FILE__, __LINE__, __VA_ARGS__);

// LOG(INFO,"test:%s\n",str)
#define LOG(verbosityname, ...)                                                \
  VLOG(what::Log::Verbosity::Verbosity##verbosityname, __VA_ARGS__)

// TODO
//* 对log系统进行初始化
void Init(int argc, char *argv[]);

auto Add_file(const char *path_in, FileMode filemode, Verbosity verbosity)
    -> bool;

void add_callBack(void *user_data, call_back_handler_t call,
                  flush_handler_t flush, close_handler_t close,
                  Verbosity max_verbosity);

//* 程序退出时的执行的函数
void exit();

auto get_verbosity_name(Verbosity verbosity) -> const char *;

void Set_thread_name(const char *str);

typedef std::vector<CallBack> CallBacks;

static int64_t start_time{
    std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count()};
static CallBacks callBacks{};
static int flush_interval_ms{0};
static bool need_flush{false};
static int8_t MAXVERBOSITY_TO_STDERR{
    static_cast<int8_t>(Verbosity::VerbosityINFO)};
static std::thread *flush_thread{nullptr};

void log_to_everywhere(Verbosity verbosity, const char *file, unsigned line,
                       const char *message);

// TODO 如何解决fatal信息
void handle_fatal_message();

void log_message(Verbosity verbosity, Message &message);

auto vastextprint(const char *format, va_list list) -> Text;

void print_prefix(char *prefix, size_t prefix_len, Verbosity verbosity,
                  const char *file, unsigned int line);

void get_thread_name(char *thread_name, size_t thread_name_len);

auto filename(const char *) -> const char *;

auto home_dir() -> const char *;

auto create_dir(const char *) -> bool;

void flush();

/******** file call back ********/
void file_log(void *user_data, Message &message);
void file_flush(void *user_data);
void file_close(void *user_data);

} // namespace what::Log

#endif