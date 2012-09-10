/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Simon Gomizelj, 2012
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <getopt.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>

enum action {
    ACTION_PRINT,
    ACTION_ADD,
    ACTION_KILL,
    ACTION_LIST,
    ACTION_INVALID
};

static char *get_key_path(const char *home, const char *fragment)
{
    char *out;

    /* path exists, add it */
    if (access(fragment, F_OK) == 0)
        return strdup(fragment);

    /* assume it's a key in $HOME/.ssh */
    if (asprintf(&out, "%s/.ssh/%s", home, fragment) < 0)
        err(EXIT_FAILURE, "failed to allocate memory");

    return out;
}

static void add_keys(char **keys, int count, int first_run)
{
    /* command + end-of-opts + NULL + keys */
    char *argv[count + 3];
    struct passwd *pwd;
    int i;

    if (count == 0 && !first_run)
        errx(EXIT_FAILURE, "no keys specified");

    pwd = getpwuid(getuid());
    if (pwd == NULL || pwd->pw_dir == NULL)
        /* unlikely */
        err(EXIT_FAILURE, "failed to lookup passwd entry");

    argv[0] = "/usr/bin/ssh-add";
    argv[1] = "--";

    for (i = 0; i < count; i++)
        argv[2 + i] = get_key_path(pwd->pw_dir, keys[i]);

    argv[2 + count] = NULL;

    execv(argv[0], argv);
    err(EXIT_FAILURE, "failed to launch ssh-add");
}

static void print_env(struct agent_data_t *data)
{
    if (data->gpg[0])
        printf("export GPG_AGENT_INFO='%s'\n", data->gpg);

    printf("export SSH_AUTH_SOCK='%s'\n",  data->sock);
    printf("export SSH_AGENT_PID='%ld'\n", (long)data->pid);
}

static int get_agent(struct agent_data_t *data)
{
    union {
        struct sockaddr sa;
        struct sockaddr_un un;
    } sa;

    int rc, flags;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        err(EXIT_FAILURE, "couldn't create socket");

    memset(&sa, 0, sizeof(sa));
    sa.un.sun_family = AF_UNIX;
    memcpy(&sa.un.sun_path[1], &SOCK_PATH[1], sizeof(SOCK_PATH) + 1);

    /* set non-blocking */
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags|O_NONBLOCK);

    if (connect(fd, &sa.sa, sizeof(SOCK_PATH) + 1) < 0)
        err(EXIT_FAILURE, "failed to connect");

    for (;;) {
        rc = read(fd, data, sizeof(*data));
        if (rc < 0) {
            if (errno != EAGAIN) {
                warn("failed to receive data from server");
                break;
            }
        } else
            break;
    }

    close(fd);
    return rc;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
    fprintf(out, "usage: %s [options] [files ...]\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help       display this help and exit\n"
        " -v, --version    display version\n"
        " -a, --add        also add keys (default)\n"
        " -k, --kill       kill the running ssh-agent\n"
        " -l, --list       list loaded keys\n"
        " -p, --print      print out environmental arguments\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    struct agent_data_t data;
    enum action verb = ACTION_ADD;

    static const struct option opts[] = {
        { "help",    no_argument, 0, 'h' },
        { "version", no_argument, 0, 'v' },
        { "add",     no_argument, 0, 'a' },
        { "kill",    no_argument, 0, 'k' },
        { "list",    no_argument, 0, 'l' },
        { "print",   no_argument, 0, 'p' },
        { 0, 0, 0, 0 }
    };

    while (true) {
        int opt = getopt_long(argc, argv, "hlvakp", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'v':
            printf("%s %s\n", program_invocation_short_name, ENVOY_VERSION);
            return 0;
        case 'a':
            verb = ACTION_ADD;
            break;
        case 'k':
            verb = ACTION_KILL;
            break;
        case 'l':
            verb = ACTION_LIST;
            break;
        case 'p':
            verb = ACTION_PRINT;
            break;
        default:
            usage(stderr);
        }
    }

    switch (get_agent(&data)) {
    case -1:
        err(EXIT_FAILURE, "failed to read data");
    case 0:
        errx(EXIT_FAILURE, "recieved no data, did ssh-agent fail to start?");
    default:
        break;
    }

    setenv("SSH_AUTH_SOCK", data.sock, true);

    switch (verb) {
    case ACTION_PRINT:
        print_env(&data);
        break;
    case ACTION_ADD:
        if (data.first_run)
            print_env(&data);
        add_keys(&argv[optind], argc - optind, data.first_run);
        break;
    case ACTION_KILL:
        kill(data.pid, SIGTERM);
        break;
    case ACTION_LIST:
        execl("/usr/bin/ssh-add", "ssh-add", "-l", NULL);
        err(EXIT_FAILURE, "failed to launch ssh-add");
    default:
        break;
    }

    return 0;
}

// vim: et:sts=4:sw=4:cino=(0
