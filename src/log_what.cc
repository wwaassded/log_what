#include "log_what.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <sys/stat.h>

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

static std::recursive_mutex locker;

static pthread_key_t thread_key;                       // "static" thread local
static pthread_once_t thread_once = PTHREAD_ONCE_INIT; // call only once

void Init(int argc, char *argv[]) {
  // TODO parse args
  atexit(exit);
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

void log_to_everywhere(Verbosity verbosity, const char *file, unsigned line,
                       const char *message) {
  char prefix[PREFIX_WIDTH];
  print_prefix(prefix, sizeof prefix, verbosity, file, line);
  Message real_message = Message{verbosity, file, line, prefix, message};
  log_message(verbosity, real_message);
}

void log_message(Verbosity verbosity, Message &message) {
  std::unique_lock<std::recursive_mutex> lock(locker);
  if (verbosity == Verbosity::VerbosityFATAL) {
    // handle_fatal_message();
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