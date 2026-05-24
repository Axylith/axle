#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

struct ResourceSample {
    double timestamp_ms;
    long rss_kb;
    long vm_kb;
    double cpu_percent;
};

inline void read_mem_kb(long& vm_kb, long& rss_kb) {
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) { vm_kb = rss_kb = 0; return; }
    long total_pages = 0, resident_pages = 0;
    fscanf(f, "%ld %ld", &total_pages, &resident_pages);
    fclose(f);
    vm_kb  = total_pages * 4;
    rss_kb = resident_pages * 4;
}

struct ResourceMonitor {
    FILE* csv = nullptr;
    long prev_utime = 0;
    long prev_stime = 0;
    double prev_time_ms = 0.0;
    long ticks_per_sec = 100;

    static constexpr double MIN_INTERVAL_MS = 100.0;

    bool open(const char* path) {
        csv = fopen(path, "w");
        if (!csv) return false;
        fprintf(csv, "timestamp_ms,rss_kb,vm_kb,cpu_percent\n");

        ticks_per_sec = sysconf(_SC_CLK_TCK);
        if (ticks_per_sec <= 0) ticks_per_sec = 100;

        FILE* f = fopen("/proc/self/stat", "r");
        if (f) {
            fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld",
                   &prev_utime, &prev_stime);
            fclose(f);
        }
        return true;
    }

    void sample(double timestamp_ms) {
        if (!csv) return;

        long rss = 0, vm = 0;
        read_mem_kb(vm, rss);

        double cpu = 0.0;
        double time_delta = timestamp_ms - prev_time_ms;

        if (time_delta >= MIN_INTERVAL_MS) {
            long utime = 0, stime = 0;
            FILE* f = fopen("/proc/self/stat", "r");
            if (f) {
                fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld",
                       &utime, &stime);
                fclose(f);
            }

            long tick_delta = (utime + stime) - (prev_utime + prev_stime);
            double ms_per_tick = 1000.0 / (double)ticks_per_sec;
            double cpu_time_ms = (double)tick_delta * ms_per_tick;
            cpu = (cpu_time_ms / time_delta) * 100.0;

            if (cpu < 0.0) cpu = 0.0;
            if (cpu > 3200.0) cpu = 0.0;

            prev_utime = utime;
            prev_stime = stime;
            prev_time_ms = timestamp_ms;
        }

        fprintf(csv, "%.1f,%ld,%ld,%.2f\n", timestamp_ms, rss, vm, cpu);
    }

    void close() {
        if (csv) {
            fflush(csv);
            fclose(csv);
            csv = nullptr;
        }
    }
};