#include "log/log.hpp"

#include <sys/stat.h>

Log::Log()
{
    log_lines_counts_ = 0;
    is_async_ = false;
    fp_ = NULL;
    buffer_ = NULL;
    log_queue_ = NULL;
    close_log_ = 0;
    today_ = 0;
    log_max_lines_ = 0;
    log_buffer_size_ = 0;
    memset(dir_name_, '\0', sizeof(dir_name_));
    memset(log_name_, '\0', sizeof(log_name_));
}

Log::~Log()
{
    mutex_.lock();
    if (fp_ != NULL)
    {
        fflush(fp_);
        fclose(fp_);
        fp_ = NULL;
    }
    if (buffer_)
    {
        delete[] buffer_;
        buffer_ = NULL;
    }
    if (log_queue_)
    {
        delete log_queue_;
        log_queue_ = NULL;
    }
    mutex_.unlock();
}

bool Log::init(const char *file_name, int close_log, int log_buf_size, int max_lines, int max_queue_size)
{
    // 设置了max_queue_size时，设置异步
    if (max_queue_size)
    {
        is_async_ = true;
        log_queue_ = new block_queue<std::string>(max_queue_size);
        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    close_log_ = close_log;
    log_buffer_size_ = log_buf_size;
    buffer_ = new char[log_buf_size];
    memset(buffer_, '\0', log_buffer_size_);
    log_max_lines_ = max_lines;

    time_t cur_time = time(NULL);
    struct tm *sys_time_info = localtime(&cur_time); // 系统时间
    // 获取本地时间,年月日作为日志文件名称
    int year = sys_time_info->tm_year + 1900; // 年份需要加上1900
    int month = sys_time_info->tm_mon + 1;    // 月份从0开始，需要加1
    int day = sys_time_info->tm_mday;
    today_ = day;
    // 判断file_name 是路径目录还是文件
    char log_full_name[256] = {0};

    const char *p = strrchr(file_name, '/'); // 查找最后一个/所在位置，其前面是目录，后面是文件名
    if (p != NULL)
    {
        size_t dir_len = static_cast<size_t>(p - file_name + 1);
        if (dir_len >= sizeof(dir_name_))
            dir_len = sizeof(dir_name_) - 1;
        memcpy(dir_name_, file_name, dir_len);
        dir_name_[dir_len] = '\0';

        strncpy(log_name_, p + 1, sizeof(log_name_) - 1);
        log_name_[sizeof(log_name_) - 1] = '\0';

        // 尽力创建目录：dir_name_ 末尾带 '/'
        {
            char tmp[256] = {0};
            snprintf(tmp, sizeof(tmp), "%s", dir_name_);
            size_t len = strlen(tmp);
            if (len > 0 && tmp[len - 1] == '/')
                tmp[len - 1] = '\0';
            if (tmp[0] != '\0')
            {
                // 逐级 mkdir，已存在则忽略
                for (char *s = tmp + ((tmp[0] == '/') ? 1 : 0); *s; ++s)
                {
                    if (*s == '/')
                    {
                        *s = '\0';
                        mkdir(tmp, 0755);
                        *s = '/';
                    }
                }
                mkdir(tmp, 0755);
            }
        }

        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name_, year, month, day, log_name_);
    }
    else
    { // 日志文件为单文件
        // file_name 不带目录时，log_name_ 就是 file_name
        strncpy(log_name_, file_name, sizeof(log_name_) - 1);
        log_name_[sizeof(log_name_) - 1] = '\0';
        dir_name_[0] = '\0';
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", year, month, day, log_name_);
    }
    // 打开文件
    fp_ = fopen(log_full_name, "a");
    if (fp_ == NULL)
        return false;

    return true;
}

bool Log::write_log(std::string level, const char *format, ...)
{
    // 日志时间信息
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *local_time = localtime(&t);
    int year = local_time->tm_year + 1900;
    int month = local_time->tm_mon + 1;
    int day = local_time->tm_mday;
    int hour = local_time->tm_hour;
    int min = local_time->tm_min;
    int sec = local_time->tm_sec;
    long int usec = now.tv_usec;

    char surfix[16] = {0}; // 等级前缀
    if (level == "Debug")
        strcpy(surfix, "[Debug]--");
    else if (level == "Info")
        strcpy(surfix, "[Info]--");
    else if (level == "Warn")
        strcpy(surfix, "[Warn]--");
    else if (level == "Error")
        strcpy(surfix, "[Error]--");

    // 写日志
    mutex_.lock();
    log_lines_counts_++;

    if (today_ != day || log_lines_counts_ % log_max_lines_ == 0)
    { // 当前要记录的日志不在当天或达到最大行数，则另创新一天的日志文件，再写
        // 先关闭当前的日志文件
        fflush(fp_);
        fclose(fp_);
        char new_log_name[256] = {0};
        char log_time_name[16] = {0}; // 日志名中的时间戳部分
        snprintf(log_time_name, 16, "_%d_%02d_%02d_", year, month, day);

        if (today_ != day)
        { // 当前日期不对应时，设置新一天的日志文件名称
            snprintf(new_log_name, 256, "%s%s%s", dir_name_, log_time_name, log_name_);
            today_ = day;
            log_lines_counts_ = 0;
        }
        else
        { // 达到最大行数
            snprintf(new_log_name, 256, "%s%s%s.%lld", dir_name_, log_time_name, log_name_, log_lines_counts_ / log_max_lines_);
        }
        fp_ = fopen(new_log_name, "a");
    }
    mutex_.unlock();

    // 按照格式format输出参数列表中的数据
    va_list args;
    va_start(args, format);

    mutex_.lock();
    // 写入每一段信息前面的时间信息和日志等级
    int n = snprintf(buffer_, 48, "[%d年-%02d月-%02d日 %02dh:%02dm:%02ds] %s ", year, month, day, hour, min, sec, surfix);
    int m = vsnprintf(buffer_ + n, log_buffer_size_ - n - 1, format, args); // 按照格式写入数据
    buffer_[n + m] = '\n';
    buffer_[n + m + 1] = '\0';
    std::string log_str = buffer_;
    mutex_.unlock();

    // 写入文件
    if (is_async_ && !log_queue_->isFull())
    {
        log_queue_->push(log_str);
    }
    else
    {
        mutex_.lock();
        fputs(log_str.c_str(), fp_);
        mutex_.unlock();
    }
    va_end(args);

    return true;
}

void Log::flush(void)
{
    mutex_.lock();
    // 强制刷新写入流缓冲区
    fflush(fp_);
    mutex_.unlock();
}
