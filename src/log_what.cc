#include "log_what.hpp"
#include <chrono>
#include <cstring>
#include <pthread.h>

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
  if (verbosity == Verbosity::VerbosityFATAL) {
    handle_fatal_message();
  }
  if (static_cast<int8_t>(verbosity) <=
      MAXVERBOSITY_TO_STDERR) { //* log to stderr
    if (verbosity > Verbosity::VerbosityWARNING) {
      fprintf(stderr, "%s%s%s%s%s", terminal_reset(), terminal_dim(),
              message.prefix, message.raw_message, terminal_reset());
    } else {
      fprintf(stderr, "%s%s%s%s%s%s", terminal_reset(), terminal_dim(),
              verbosity < Verbosity::VerbosityWARNING ? terminal_red()
                                                      : terminal_yellow(),
              message.prefix, message.raw_message, terminal_reset());
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

void print_prefix(char *prefix, size_t prefix_len, Verbosity verbosity,
                  const char *file, unsigned int line) {
  //*线程
  char thread_name[THREADNAME_WIDTH + 1] = {0};
  get_thread_name(thread_name, sizeof thread_name);

  //*时间
  tm time_info;
  auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
  auto sec_since_epoch = time_t(ms_since_epoch / 1000);
  localtime_r(&sec_since_epoch, &time_info);

  //*文件
  file = filename(file);

  //* verbosity name
  char verbosity_level[6];
  char *verbosity_name = get_verbosity_name(verbosity);
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
    bytes = snprintf(prefix + pos, prefix_len - pos, "%02d:%02d:%02d.%03lld ",
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
    snprintf(shortened_filename, FILENAME_WIDTH + 1, file);
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
    snprintf(thread_name, thread_name_len, "%*X", thread_name_len - 1, id);
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

} // namespace what::Log