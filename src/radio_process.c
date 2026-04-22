#define _POSIX_C_SOURCE 200809L

#include "radio_process.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int child_is_running(ChildProc *p)
{
    if (p->pid <= 0) return 0;
    int status = 0;
    pid_t r = waitpid(p->pid, &status, WNOHANG);
    return (r == 0);
}

void child_kill_group(ChildProc *p)
{
    if (p->pid > 0 && child_is_running(p)) {
        kill(-p->pid, SIGTERM);
        for (int i = 0; i < 20; i++) {
            if (!child_is_running(p)) break;
            sleep_ms(50);
        }
        if (child_is_running(p)) kill(-p->pid, SIGKILL);
    }
    if (p->fd_in >= 0) {
        close(p->fd_in);
        p->fd_in = -1;
    }
    if (p->fd_out >= 0) {
        close(p->fd_out);
        p->fd_out = -1;
    }
    if (p->pid > 0) {
        int st = 0;
        (void)waitpid(p->pid, &st, 0);
        p->pid = -1;
    }
}

int spawn_aplay(ChildProc *out, const RadioConfig *cfg)
{
    int pipe_in[2];
    if (pipe(pipe_in) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        return -1;
    }

    if (pid == 0) {
        char audio_rate[16];
        snprintf(audio_rate, sizeof(audio_rate), "%d", cfg->audio_rate);
        setpgid(0, 0);
        dup2(pipe_in[0], STDIN_FILENO);
        close(pipe_in[0]);
        close(pipe_in[1]);
        execlp("aplay", "aplay",
               "-q", "-D", cfg->alsa_dev,
               "-r", audio_rate,
               "-f", "S16_LE",
               "-c", "1",
               "-t", "raw",
               (char *)NULL);
        _exit(127);
    }

    close(pipe_in[0]);
    out->pid = pid;
    out->fd_in = pipe_in[1];
    out->fd_out = -1;
    return 0;
}

int spawn_rtl_fm(ChildProc *out, const RadioConfig *cfg)
{
    int pipe_out[2];
    if (pipe(pipe_out) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_out[0]);
        close(pipe_out[1]);
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_out[0]);
        close(pipe_out[1]);

        char rtl_idx[16], freq[16], ppm[16], audio_rate[16];
        snprintf(rtl_idx, sizeof(rtl_idx), "%d", cfg->rtl_index);
        snprintf(freq, sizeof(freq), "%d", cfg->freq_hz);
        snprintf(ppm, sizeof(ppm), "%d", cfg->ppm);
        snprintf(audio_rate, sizeof(audio_rate), "%d", cfg->audio_rate);

        if (strcmp(cfg->gain, "auto") == 0) {
            execlp("rtl_fm", "rtl_fm",
                   "-d", rtl_idx,
                   "-f", freq,
                   "-M", "wbfm",
                   "-s", cfg->sdr_rate,
                   "-r", audio_rate,
                   "-A", cfg->atan_mode,
                   "-F", "9",
                   "-E", "deemp",
                   "-E", "dc",
                   "-l", "0",
                   "-p", ppm,
                   "-",
                   (char *)NULL);
        } else {
            execlp("rtl_fm", "rtl_fm",
                   "-d", rtl_idx,
                   "-f", freq,
                   "-M", "wbfm",
                   "-s", cfg->sdr_rate,
                   "-r", audio_rate,
                   "-A", cfg->atan_mode,
                   "-F", "9",
                   "-E", "deemp",
                   "-E", "dc",
                   "-l", "0",
                   "-p", ppm,
                   "-g", cfg->gain,
                   "-",
                   (char *)NULL);
        }
        _exit(127);
    }

    close(pipe_out[1]);
    out->pid = pid;
    out->fd_in = -1;
    out->fd_out = pipe_out[0];
    return 0;
}
