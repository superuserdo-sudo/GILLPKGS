
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

#define VERSION "0.1.0"
#define CYAN  "\033[1;36m"
#define GREEN "\033[1;32m"
#define DIM   "\033[90m"
#define RESET "\033[0m"

static const char *art[] = {
"                        #@@@@@@@",
"                 %@@@@@*        %@@@",
"              -@@#                  @@",
"             @@                       @@",
"            @@          =              @@",
"           @@       =@@@@@@@            @:",
"           @        @@@@@@@@% *@@@@     @@",
"          @@       @@@@@@@@@@@@@@@@@    #@",
"          @@       @        @@@@@@@@@    @",
"          @@      %@    @   @    @ %@    @",
"          @@      @@   @-   @   @ *%@   .@",
"          @@      @@   @@@  @   @@#@=   @@",
"           @      @@       @@      @    @%",
"           @@      @+     #@@@   +@    @@",
"            @@      @@@*@@@  %@@@:    @@",
"             @@                      @@",
"              =@@                 @@@@",
"             %@# @@@@@@*==*@@@@@@@    @%",
"            @@                         @@",
"           +@@                        @@@",
"           @@%@@*                 +@@@  @@",
"           @%    @@@@@@:    #@@@@@      @@",
"           @@       @@        @@#      %@@",
"           @@@@%@@@@            :@@@@@@ @#",
"            @*                         @@",
"             @@      @@@@   @@@@      @@",
"              @@    @*  @@ @@  +@:  @@@",
"                @@@@*    @ @    -@@@",
"                  @@    @@@@@    @#",
"                  @@    %@@@     @@",
"                  #@    @   @    @-",
"                   @@@@@     @@@@@"
};

#define ART_COUNT (sizeof(art) / sizeof(art[0]))

static void get_os(char *out, size_t size)
{
    FILE *file = fopen("/etc/os-release", "r");
    char line[512];

    snprintf(out, size, "Unknown");

    if (!file)
        return;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *value = line + 12;
            size_t len = strlen(value);

            if (len && value[len - 1] == '\n')
                value[--len] = '\0';

            if (len >= 2 && value[0] == '"' &&
                value[len - 1] == '"') {
                value[len - 1] = '\0';
                value++;
            }

            snprintf(out, size, "%s", value);
            break;
        }
    }

    fclose(file);
}

static void get_uptime(char *out, size_t size)
{
    FILE *file = fopen("/proc/uptime", "r");
    double seconds = 0;

    if (!file || fscanf(file, "%lf", &seconds) != 1) {
        snprintf(out, size, "Unavailable");
        if (file)
            fclose(file);
        return;
    }

    fclose(file);

    unsigned long total = (unsigned long)seconds;
    unsigned long days = total / 86400;
    unsigned long hours = (total % 86400) / 3600;
    unsigned long mins = (total % 3600) / 60;

    if (days)
        snprintf(out, size, "%lud %luh %lum", days, hours, mins);
    else if (hours)
        snprintf(out, size, "%luh %lum", hours, mins);
    else
        snprintf(out, size, "%lum", mins);
}

int main(void)
{
    struct utsname sys;
    char os[160], uptime[64], hostname[128];

    const char *user = getenv("USER");
    const char *shell = getenv("SHELL");
    const char *home = getenv("HOME");

    int color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;

    if (uname(&sys) != 0) {
        fprintf(stderr, "Gfetch: unable to read system information\n");
        return 1;
    }

    get_os(os, sizeof(os));
    get_uptime(uptime, sizeof(uptime));

    if (gethostname(hostname, sizeof(hostname)) != 0)
        snprintf(hostname, sizeof(hostname), "unknown");

    hostname[sizeof(hostname) - 1] = '\0';

    if (!user) user = "unknown";
    if (!shell) shell = "Unknown";
    if (!home) home = "Unknown";

    const char *cyan = color ? CYAN : "";
    const char *green = color ? GREEN : "";
    const char *dim = color ? DIM : "";
    const char *reset = color ? RESET : "";

    printf("\n");

    for (size_t i = 0; i < ART_COUNT; i++) {
        printf("%s%s%s", cyan, art[i], reset);

        switch (i) {
        case 1:
            printf("   %s%s@%s%s", green, user, hostname, reset);
            break;
        case 2:
            printf("   %s------------------------%s", dim, reset);
            break;
        case 4:
            printf("   %sOS       %s%s", green, reset, os);
            break;
        case 6:
            printf("   %sKernel   %s%s %s",
                   green, reset, sys.sysname, sys.release);
            break;
        case 8:
            printf("   %sArch     %s%s", green, reset, sys.machine);
            break;
        case 10:
            printf("   %sUptime   %s%s", green, reset, uptime);
            break;
        case 12:
            printf("   %sShell    %s%s", green, reset, shell);
            break;
        case 14:
            printf("   %sHome     %s%s", green, reset, home);
            break;
        case 16:
            printf("   %sGfetch   %sv%s", green, reset, VERSION);
            break;
        case 18:
            printf("   %s------------------------%s", dim, reset);
            break;
        default:
            break;
        }

        putchar('\n');
    }

    putchar('\n');
    return 0;
}
