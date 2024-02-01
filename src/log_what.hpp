#include <cassert>
#include <define>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace what::Log {

typedef void (*call_back_handler_t)(void *user_data, Message &);
typedef void (*flush_handler_t)(void *user_data);
typedef void (*close_handler_t)(void *user_data);

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

const char *terminal_red() { return VTSEQ(31); }
const char *terminal_green() { return VTSEQ(32); }
const char *terminal_yellow() { return VTSEQ(33); }
const char *terminal_dim() { return VTSEQ(2); }
//! start and end with it everytime
const char *terminal_reset() { return VTSEQ(0); }

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

// TODO
//* 对log系统进行初始化
void Init(int argc, char *argv[]);

//* 程序退出时的执行的函数
void exit();

enum class Verbosity {
  VerbosityFATAL = -3,
  VerbosityERROR = -2,
  VerbosityWARNING = -1,
  VerbosityINFO = 0,
  VerbosityMESSAGE = 1, //普通的信息
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

  const char *prefix;

  const char *raw_message;
};

class CallBack {
public:
  void *user_data;

  call_back_handler_t call_back;

  flush_handler_t flush;

  close_handler_t close;

  Verbosity max_verbosit;
};

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

void log(Verbosity verbosity, const char *file, unsigned int line,
         const char *format, ...);

void log_to_everywhere(Verbosity verbosity, const char *file, unsigned line,
                       const char *message);

// TODO 如何解决fatal信息
void handle_fatal_message();

void log_message(Verbosity verbosity, Message &message);

auto vastextprint(const char *format, va_list list) -> Text;

void print_prifix(char *prefix, size_t prefix_len, Verbosity verbosity,
                  const char *file, unsigned int line);

void get_thread_name(char *thread_name, size_t thread_name_len);

auto filename(const char *) -> const char *;

void flush();

} // namespace what::Log