#include "log_what.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <mutex>
#include <pthread.h>
#include <regex>
#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace what::Log {

/*********************************Class Text*********************************/
Text::Text(Text &&other) {
  __str = other.__str;
  other.__str = nullptr;
}

Text::~Text() {
  if (!__str) {
    free(__str);
    __str = nullptr;
  }
}

auto Text::Release() -> char * {
  auto str = __str;
  __str = nullptr;
  return str;
}

/*********************************log function*********************************/
static std::vector<std::pair<std::string, std::string>> s_user_stack_cleanups;

typedef std::vector<CallBack> CallBacks;

const auto start_time = std::chrono::steady_clock::now();

static CallBacks callBacks{};

static signal_t internal_sig{};

static bool need_flush{false};
static int8_t MAXVERBOSITY_TO_STDERR{
    static_cast<int8_t>(Verbosity::VerbosityINFO)};
static std::thread *flush_thread{nullptr};

static std::recursive_mutex locker;

static pthread_key_t thread_key;                       // "static" thread local
static pthread_once_t thread_once = PTHREAD_ONCE_INIT; // call only once

void Init(int argc, char *argv[]) {
  // TODO parse args
  install_signal_handler(internal_sig);
  atexit(exit);
}

void write_to_stderr(const char *str) { write_to_stderr(str, strlen(str)); }

void write_to_stderr(const char *str, size_t len) {
  auto ignore = write(STDERR_FILENO, str, len);
  (void)ignore;
}

void call_default_signal_handler(
    int signal_number) { //恢复我们的处理函数后杀死进程
  struct sigaction sig_action;
  sigemptyset(&sig_action.sa_mask);
  sig_action.sa_handler = SIG_DFL;
  sigaction(signal_number, &sig_action, nullptr);
  kill(getpid(), signal_number);
}

void signal_handler(int signum, siginfo_t *siginfo, void *ptr) {
  char *signame = "UNKNOW SIG";
  if (signum == SIGABRT)
    signame = "SIGABRT";
//! unsafe code may cause dead lock
#ifdef UNSAFE
  write_to_stderr(TERMINAL_RESET);
  write_to_stderr(TERMINAL_BOLD);
  write_to_stderr(TERMINAL_LIGHT_RED);
  write_to_stderr("\n");
  write_to_stderr("log_what capture a sig:");
  write_to_stderr(signame);
  write_to_stderr("\n");
  write_to_stderr(TERMINAL_RESET);
  flush();

  char prefix[PREFIX_WIDTH];
  print_prefix(prefix, PREFIX_WIDTH, Verbosity::VerbosityFATAL, "", 0);
  auto message = Message{
      .verbosity = Verbosity::VerbosityFATAL,
      .file = nullptr,
      .line = 0,
      .prefix = prefix,
      .raw_message = signame,
  };
  log_message(Verbosity::VerbosityFATAL, message);
  flush();

#endif

  call_default_signal_handler(signum);
}

void install_signal_handler(const signal_t &sig) {
  struct sigaction sig_action;
  sigemptyset(&sig_action.sa_mask);
  sig_action.sa_flags |= SA_SIGINFO;
  sig_action.sa_sigaction = signal_handler;
  if (sig.sigabrt) {
    ASSERT(sigaction(SIGABRT, &sig_action, NULL) != -1,
           "failed to install sigabrt");
  }
  // TODO wait for more signal
}

void exit() {
  LOG(INFO, "on exit");
  flush();
  if (!flush_thread)
    delete flush_thread;
}

void init_thread_key() { pthread_key_create(&thread_key, free); }

void Set_thread_name(const char *str) {
  ASSERT(str != nullptr, "str should not be null !");
  pthread_once(&thread_once, init_thread_key);
  pthread_setspecific(thread_key, strdup(str)); // strdup->malloc->free
}

void log(Verbosity verbosity, const char *file, unsigned int line,
         const char *format, ...) {
  va_list list;
  va_start(list, format);
  auto buffer = vastextprint(format, list);
  log_to_everywhere(verbosity, file, line, buffer.C_str());
  va_end(list);
}

void raw_log(Verbosity verbosity, const char *file, unsigned int line,
             const char *format, ...) {
  va_list list;
  va_start(list, format);
  auto buffer = vastextprint(format, list);
  auto message = Message{
      .verbosity = verbosity,
      .file = file,
      .line = line,
      .prefix = nullptr,
      .raw_message = buffer.C_str(),
  };
  log_message(verbosity, message);
  va_end(list);
}

void log_to_everywhere(Verbosity verbosity, const char *file, unsigned line,
                       const char *message) {
  char prefix[PREFIX_WIDTH];
  print_prefix(prefix, sizeof prefix, verbosity, file, line);
  Message real_message = Message{verbosity, file, line, prefix, message};
  log_message(verbosity, real_message);
}

void do_replacements(
    const std::vector<std::pair<std::string, std::string>> &replacements,
    std::string &str) {
  for (auto &&p : replacements) {
    if (p.first.size() <= p.second.size()) {
      // On gcc, "type_name<std::string>()" is "std::string"
      continue;
    }

    size_t it;
    while ((it = str.find(p.first)) != std::string::npos) {
      str.replace(it, p.first.size(), p.second);
    }
  }
}

static const std::vector<std::pair<std::string, std::string>> REPLACE_LIST = {

    {"std::__1::", "std::"},
    {"__thiscall ", ""},
    {"__cdecl ", ""},
};

std::string prettify_stacktrace(const std::string &input) {
  std::string output = input;

  do_replacements(s_user_stack_cleanups, output);
  do_replacements(REPLACE_LIST, output);

  try {
    std::regex std_allocator_re(R"(,\s*std::allocator<[^<>]+>)");
    output = std::regex_replace(output, std_allocator_re, std::string(""));

    std::regex template_spaces_re(R"(<\s*([^<> ]+)\s*>)");
    output =
        std::regex_replace(output, template_spaces_re, std::string("<$1>"));
  } catch (std::regex_error &) {
    // Probably old GCC.
  }

  return output;
}

std::string stacktrace_as_stdstring(int skip) {
  // From https://gist.github.com/fmela/591333
  void *callstack[128];
  const auto max_frames = sizeof(callstack) / sizeof(callstack[0]);
  int num_frames = backtrace(callstack, max_frames);
  char **symbols = backtrace_symbols(callstack, num_frames);

  std::string result;
  // Print stack traces so the most relevant ones are written last
  // Rationale:
  // http://yellerapp.com/posts/2015-01-22-upside-down-stacktraces.html
  for (int i = num_frames - 1; i >= skip; --i) {
    char buf[1024];
    Dl_info info;
    if (dladdr(callstack[i], &info) && info.dli_sname) {
      char *demangled = NULL;
      int status = -1;
      if (info.dli_sname[0] == '_') {
        demangled = abi::__cxa_demangle(info.dli_sname, 0, 0, &status);
      }
      snprintf(buf, sizeof(buf), "%-3d %*p %s + %zd\n", i - skip,
               int(2 + sizeof(void *) * 2), callstack[i],
               status == 0           ? demangled
               : info.dli_sname == 0 ? symbols[i]
                                     : info.dli_sname,
               static_cast<char *>(callstack[i]) -
                   static_cast<char *>(info.dli_saddr));
      free(demangled);
    } else {
      snprintf(buf, sizeof(buf), "%-3d %*p %s\n", i - skip,
               int(2 + sizeof(void *) * 2), callstack[i], symbols[i]);
    }
    result += buf;
  }
  free(symbols);

  if (num_frames == max_frames) {
    result = "[truncated]\n" + result;
  }

  if (!result.empty() && result[result.size() - 1] == '\n') {
    result.resize(result.size() - 1);
  }

  return prettify_stacktrace(result);
}

auto get_stack() -> Text {
  auto str = stacktrace_as_stdstring(4);
  return Text(strdup(str.c_str()));
}

void handle_fatal_message() {
  auto text = get_stack();
  if (!text.Is_empty())
    RAW_LOG(ERROR, "Stack Trace:\n %s", text.C_str());
  return;
}

void log_message(Verbosity verbosity, Message &message) {
  std::unique_lock<std::recursive_mutex> lock(locker);
  if (verbosity == Verbosity::VerbosityFATAL) {
    handle_fatal_message();
  }
  if (static_cast<int8_t>(verbosity) <=
      MAXVERBOSITY_TO_STDERR) { //* log to stderr
    if (verbosity > Verbosity::VerbosityWARNING) {
      fprintf(stderr, "%s%s%s%s%s\n", TERMINAL_RESET, TERMINAL_DIM,
              message.prefix, message.raw_message, TERMINAL_RESET);
    } else {
      fprintf(stderr, "%s%s%s%s%s%s\n", TERMINAL_RESET, TERMINAL_DIM,
              verbosity < Verbosity::VerbosityWARNING ? TERMINAL_RED
                                                      : TERMINAL_YELLOW,
              message.prefix, message.raw_message, TERMINAL_RESET);
    }
    if (flush_interval_ms == 0) {
      fflush(stderr);
    } else {
      need_flush = true;
    }
  }
  for (auto &callBack : callBacks) { //* log to registered callback
    if (verbosity <= callBack.max_verbosit) {
      callBack.call_back(callBack.user_data, message);
      if (flush_interval_ms == 0) {
        if (callBack.flush) {
          callBack.flush(callBack.user_data);
        }
      } else {
        need_flush = true;
      }
    }
  }

  if (flush_interval_ms != 0 && flush_thread == nullptr) {
    flush_thread = new std::thread([] { // TODO require delete()
      while (true) {
        if (need_flush) {
          flush();
        } else {
          std::this_thread::sleep_for(
              std::chrono::milliseconds(flush_interval_ms));
        }
      }
    });
  }

  if (message.verbosity == Verbosity::VerbosityFATAL) {
    flush();
    signal(SIGABRT, SIG_DFL);
  }
}

// TODO
auto get_verbosity_name(Verbosity verbosity) -> const char * {
  switch (verbosity) {
  case Verbosity::VerbosityERROR:
    return "ERR";
  case Verbosity::VerbosityINFO:
    return "INFO";
  case Verbosity::VerbosityFATAL:
    return "FATL";
  case Verbosity::VerbosityMESSAGE:
    return "MES";
  case Verbosity::VerbosityWARNING:
    return "WARN";
  default:
    return nullptr;
  }
}

void print_prefix(char *prefix, size_t prefix_len, Verbosity verbosity,
                  const char *file, unsigned int line) {
  //*线程
  char thread_name[THREADNAME_WIDTH + 1] = {0};
  get_thread_name(thread_name, sizeof thread_name);

  //*时间
  tm time_info;
  auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
  auto sec_since_epoch = time_t(ms_since_epoch / 1000);
  localtime_r(&sec_since_epoch, &time_info);

  auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start_time)
                       .count();
  auto uptime_sec = static_cast<double>(uptime_ms) / 1000.0;

  //*文件
  file = filename(file);

  //* verbosity name
  char verbosity_level[6];
  const char *verbosity_name = get_verbosity_name(verbosity);
  if (verbosity_name) {
    snprintf(verbosity_level, sizeof verbosity_level - 1, "%s", verbosity_name);
  } else {
    ASSERT(verbosity_name != nullptr, "fail to get verbosity name!");
  }

  //* print prefix
  size_t pos = 0;
  // print date
  int bytes;
  bytes =
      snprintf(prefix, prefix_len, "%04d-%02d-%02d ", 1900 + time_info.tm_year,
               1 + time_info.tm_mon, time_info.tm_mday);
  if (bytes > 0)
    pos += bytes;

  if (pos < prefix_len) {
    // print time
    bytes = snprintf(prefix + pos, prefix_len - pos, "%02d:%02d:%02d.%03ld ",
                     time_info.tm_hour, time_info.tm_min, time_info.tm_sec,
                     ms_since_epoch % 1000);
    if (bytes > 0)
      pos += bytes;
  }

  if (pos < prefix_len) {
    // print uptime
    bytes = snprintf(prefix + pos, prefix_len - pos, "(%8.3fs)", uptime_sec);
    if (bytes > 0) {
      pos += bytes;
    }
  }

  if (pos < prefix_len) {
    // print thread name
    bytes = snprintf(prefix + pos, prefix_len - pos, "[%-*s]", THREADNAME_WIDTH,
                     thread_name);
    if (bytes > 0)
      pos += bytes;
  }

  if (pos < prefix_len) {
    // print filename linenumber
    //文件名字过长会被裁减
    char shortened_filename[FILENAME_WIDTH + 1];
    snprintf(shortened_filename, FILENAME_WIDTH + 1, "%s", file);
    bytes = snprintf(prefix + pos, prefix_len - pos, "%*s:%-5u ",
                     FILENAME_WIDTH, shortened_filename, line);
    if (bytes > 0)
      pos += bytes;
  }

  if (pos < prefix_len) {
    // print verbosity
    bytes = snprintf(prefix + pos, prefix_len - pos, "%6s| ", verbosity_level);
    if (bytes > 0)
      pos += bytes;
  }
}

void get_thread_name(char *thread_name, size_t thread_name_len) {
  thread_name[thread_name_len - 1] = '\0';
  pthread_once(&thread_once, init_thread_key);
  if (char *name = reinterpret_cast<char *>(pthread_getspecific(thread_key))) {
    snprintf(thread_name, thread_name_len, "%s", name);
  } else {
    thread_name[0] = '\0';
  }

  if (thread_name[0] ==
      '\0') { // fail to get a specific thread name give it a hex number
    auto id = pthread_self();
    snprintf(thread_name, thread_name_len, "%*lX",
             static_cast<int>(thread_name_len) - 1, id);
  }
}

auto vastextprint(const char *format, va_list list) -> Text {
  char *buffer;
  int bytes = vasprintf(&buffer, format, list);
  ASSERT(bytes >= 0,
         "bad format fatal"); // TODO 应该使用自己的fatal log方式进行处理
  return Text(buffer);
}

auto filename(const char *file) -> const char * {
  for (auto ptr = file; *ptr; ++ptr) {
    if (*ptr == '/' || *ptr == '\\') { //消除掉文件名的前缀
      file = ptr + 1;
    }
  }
  return file;
}

void flush() {
  std::unique_lock<std::recursive_mutex> lock(locker);
  fflush(stderr);
  for (auto &callback : callBacks) {
    if (callback.flush) {
      callback.flush(callback.user_data);
    }
  }
  need_flush = false;
}

auto Add_file(const char *path_in, FileMode filemode, Verbosity verbosity)
    -> bool {
  char path[FILENAME_MAX];
  if (path_in[0] == '~') {
    snprintf(path, FILENAME_MAX, "%s%s", home_dir(), path_in);
  } else {
    snprintf(path, FILENAME_MAX, "%s", path_in);
  }
  if (!create_dir(path)) {
    LOG(ERROR, "failed to create dir:%s", path);
  }
  const char *mode = filemode == FileMode::Truncate ? "w" : "a";
  FILE *file;
  file = fopen(path, mode);
  if (!file) {
    LOG(ERROR, "failed to open file: %s", path);
    return false;
  }

  add_callBack(file, file_log, file_flush, file_close, verbosity);

  LOG(MESSAGE, "FILE:%-*s FileMode:%-*s Verbosity:%-*s", FILENAME_WIDTH,
      path_in, 5, mode, 6, get_verbosity_name(verbosity));
  return true;
}

auto create_dir(const char *filepath) -> bool {
  char *file = strdup(filepath);
  for (char *p = strchr(file + 1, '/'); p; p = strchr(p + 1, '/')) {
    *p = '\0';
    if (mkdir(file, 0755) == -1) {
      LOG(ERROR, "failed to create dir: %s", file);
      free(file);
      return false;
    }
    *p = '/';
  }
  free(file);
  return true;
}

auto home_dir() -> const char * {
  auto home = getenv("HOME");
  ASSERT(home != nullptr, "failed to get your home directory");
  return home;
}

//? 是否应该支持 remove_callBack
void add_callBack(void *user_data, call_back_handler_t call,
                  flush_handler_t flush, close_handler_t close,
                  Verbosity max_verbosity) {
  std::unique_lock<std::recursive_mutex> lock(locker);
  auto tmp = CallBack{.user_data = user_data,
                      .call_back = call,
                      .flush = flush,
                      .close = close,
                      .max_verbosit = max_verbosity};
  callBacks.push_back(std::move(tmp));
}

void file_log(void *user_data, Message &message) {
  auto file = reinterpret_cast<FILE *>(user_data);
  fprintf(file, "%s%s\n", message.prefix, message.raw_message);
  if (flush_interval_ms == 0) {
    fflush(file);
  }
}

void file_flush(void *user_data) {
  auto file = reinterpret_cast<FILE *>(user_data);
  fflush(file);
}

void file_close(void *user_data) {
  auto file = reinterpret_cast<FILE *>(user_data);
  if (file) {
    fclose(file);
  }
}

} // namespace what::Log