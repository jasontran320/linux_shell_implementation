#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
const char is_running[] = "Running";
const char is_stopped[] = "Stopped";
mode_t mode = S_IRWXU | S_IRWXG | S_IRWXO;

pid_t foreground_pid[6] = {-1, -1,-1 ,-1,-1, -1};

void parse_input(char cmd_line[81][255], char*cmd_line_points[], char input[], int * is_true, char output[], size_t size);

struct job {
    int job_id;
    int pid;
    int job_state; //0 for fg running, 1 for bg running, 2 for stopped, -1 for if it doesn't exit
    char * job_name;
    int is_foreground;
};

struct job empty_job = { -1, -1, -1, NULL, -1};

struct job different_jobs[6];

void initialize_jobs() {
    for (int i = 0; i < 6; i++) {
        different_jobs[i] = empty_job;
    }
}

void reinsert_null(char* cmd_line_points[]) {
    for (int i = 0; i < 81; ++i) {
        if (cmd_line_points[i] == NULL || strcmp(cmd_line_points[i], "") == 0) {
            cmd_line_points[i] = NULL;
            break;
        }
    }
}

struct job create_job(int jid, int p_id, int state, const char* name, int is_fg) {
    struct job new_job;
    new_job.job_id = jid;
    new_job.pid = p_id;
    new_job.job_state = state;
    new_job.job_name = strdup(name);
    new_job.is_foreground = is_fg;
    return new_job;
}

int look_for_job(int is_jid, int index) {
    if (is_jid) {
        for (int i = 0; i < 6; ++i) {
            if (different_jobs[i].job_id == index) {return i;}
        }
    }
    else {
        for (int i = 0; i < 6; ++i) {
            if (foreground_pid[i] == index) {return i;}
        }
    }
    return -1;
}

void create_name(char temp[255], char cmd_line[81][255]) {
    for (int i = 0; cmd_line[i][0] != '\0' && i < 81; ++i) {
        strcat(temp, cmd_line[i]);
        strcat(temp, " ");
    }
}

void fill_with_defaults(char cmd_line[81][255]) {
    for (int i = 0; i < 81; ++i) {
        cmd_line[i][0] = '\0';
    }
}

int index_backslash(char output[]) {
    int size = strlen(output);
    for (int i = size; i>=0; --i) {
        if (output[i] == '/') {return i;}
    }
    return -1;
}

void cd_helper(char * arg) {
    if (*arg != '\0') {
        if (chdir(arg) == 0) {}
        else {printf("cd: %s: No such file or directory\n", arg);}
    }
    else {
        char * s = getenv("HOME");
        if (s != NULL) {cd_helper(s);}
        else {printf("No HOME path set");}
    }
}

void cd(char cmd_line[81][255], char output[], size_t size) {
    if (strcmp(cmd_line[1], "..") == 0) {
        if (getcwd(output, size) != NULL) {
            int index = index_backslash(output);
            if (index >= 0) {
                output[index] = '\0';
                cd_helper(output);
            }
            else {printf("cd: No such file or directory\n");}
        }
    }
    else {cd_helper(cmd_line[1]);}
}


void pwd(char output[], size_t size) {
    if (getcwd(output, size) != NULL) {
        printf("%s\n", output);
    } else {
        printf("Unknown exception while accessing current directory\n");
    }
}

void clean_processes() {
    for (int i = 0; i < 6; ++i)
        if (foreground_pid[i] > 0 && different_jobs[i].is_foreground == 0) {kill(foreground_pid[i], SIGKILL);}
    int status; int i;
    sleep(1);
    for (i = 0; i < 6; ++i) if (different_jobs[i].job_state >= 1 && waitpid(different_jobs[i].pid, &status, WNOHANG) > 0) {
            foreground_pid[i] = -1; different_jobs[i] = empty_job;}
}

void jobs() {
    for (int i = 0; i < 6; ++i) {
        if (different_jobs[i].job_id >= 0) {
            printf("[%d] (%d) %s %s\n", different_jobs[i].job_id, different_jobs[i].pid, (different_jobs[i].job_state == 2) ? is_stopped : is_running, different_jobs[i].job_name);
        }
    }
}

void fg(char cmd_line[81][255], char output[]) {
    strcpy(output, cmd_line[1]); int index = 0; int is_jpid = 0;
    if (output[0] == '%') {index = 1; is_jpid = 1;}
    char * temp = &output[index];
    int i = strtol(temp, NULL, 10);
    int p_index = look_for_job(is_jpid, i);
    if (p_index >= 0) {
        int status;
        kill(foreground_pid[p_index], SIGCONT);
        different_jobs[p_index].job_state = 0; different_jobs[p_index].is_foreground = 1;
        waitpid(foreground_pid[p_index], &status, WUNTRACED);
        if(!WIFSTOPPED(status)) {
            foreground_pid[p_index] = -1;
            different_jobs[p_index] = empty_job;
        }
    }
}

void bg(char cmd_line[81][255], char output[]) {
    strcpy(output, cmd_line[1]); int index = 0; int is_jpid = 0;
    if (output[0] == '%') {index = 1; is_jpid = 1;}
    char * temp = &output[index];
    int i = strtol(temp, NULL, 10);
    int p_index = look_for_job(is_jpid, i);
    if (p_index >= 0) {
        kill(foreground_pid[p_index], SIGCONT);
        different_jobs[p_index].job_state = 1;
    }
}

void input_redirection(char cmd_line[81][255], char input[], int *is_true, char output[], size_t size, int index) {
    const char *filename = cmd_line[index+1];
    FILE *file = freopen(filename, "r", stdin); // Open in write mode to overwrite the file
    if (file == NULL) {
        perror("Error opening the file");
        freopen("/dev/tty", "r", stdin);
        return;
    }
    if (chmod(filename, mode) == -1) {
        perror("chmod");
    }
    char temp[81][255];
    char *temp2[81];
    char temp3[255] = "";
    fill_with_defaults(temp);
    int i = 0;
    for (; i != index && i < 81; ++i) {
        strcat(temp3, cmd_line[i]);
        strcat(temp3, " ");
    }
    if (i != 80) parse_input(temp, temp2, temp3, is_true, output, size);
    fclose(file);
    freopen("/dev/tty", "r", stdin);
}

void output_redirection(char cmd_line[81][255], char input[], int *is_true, char output[], size_t size, int index) {
    const char *filename = cmd_line[index+1];
    FILE *file = freopen(filename, "w", stdout); // Open in write mode to overwrite the file
    if (file == NULL) {
        perror("Error opening the file");
        freopen("/dev/tty", "w", stdout);
        return;
    }
    if (chmod(filename, mode) == -1) {
        perror("chmod");
    }
    char temp[81][255];
    char *temp2[81];
    char temp3[255] = "";
    fill_with_defaults(temp);
    int i = 0;
    for (; i != index && i < 81; ++i) {
        strcat(temp3, cmd_line[i]);
        strcat(temp3, " ");
    }
    if (i != 80) parse_input(temp, temp2, temp3, is_true, output, size);
    fflush(stdout);
    fclose(file);
    freopen("/dev/tty", "w", stdout);
}

void output2_redirection(char cmd_line[81][255], char input[], int *is_true, char output[], size_t size, int index) {
    const char *filename = cmd_line[index+1];
    FILE *file = freopen(filename, "a", stdout); // Open in append mode to overwrite the file
    if (file == NULL) {
        perror("Error opening the file");
        freopen("/dev/tty", "w", stdout);
        return;
    }
    if (chmod(filename, mode) == -1) {
        perror("chmod");
    }
    char temp[81][255];
    char *temp2[81];
    char temp3[255] = "";
    fill_with_defaults(temp);
    int i = 0;
    for (; i != index && i < 81; ++i) {
        strcat(temp3, cmd_line[i]);
        strcat(temp3, " ");
    }
    if (i != 80) parse_input(temp, temp2, temp3, is_true, output, size);
    fflush(stdout);
    fclose(file);
    freopen("/dev/tty", "w", stdout);
}

void quit(int * is_true) {
    clean_processes();
    *is_true = 0;
}

void handle_sigint(int signum) {
    if (signum == SIGINT) {
        int i = 0;
        for (; i < 6; ++i) {if (different_jobs[i].is_foreground == 1) break;}
        if (foreground_pid[i] > 0 && different_jobs[i].is_foreground == 1) {
            kill(foreground_pid[i], SIGINT);
            foreground_pid[i] = 0;
            different_jobs[i] = empty_job;
        }
    }
}
//Test for quick handling of multiple background processes
void handle_sigchld(int signum) {
    if (signum == SIGCHLD) {
        int status; int i;
        for (i = 0; i < 6; ++i) if (different_jobs[i].job_state == 1 && waitpid(different_jobs[i].pid, &status, WNOHANG) > 0) {
            foreground_pid[i] = -1; different_jobs[i] = empty_job; break;}
        }
    }

void handle_SIGTSTP(int signum) {
    if (signum == SIGTSTP) {
        int i; int found = 0;
        for (i = 0; i < 6; ++i) {if (different_jobs[i].is_foreground == 1) {found = 1; break;}}
        if (found) {
            kill(foreground_pid[i], SIGTSTP);
            different_jobs[i].job_state = 2;
            different_jobs[i].is_foreground = 0;
        }
    }
}

void my_kill(char cmd_line[81][255], char output[]) {
    strcpy(output, cmd_line[1]); int index = 0; int is_jpid = 0;
    if (output[0] == '%') {index = 1; is_jpid = 1;}
    char * temp = &output[index];
    int i = strtol(temp, NULL, 10);
    int p_index = look_for_job(is_jpid, i);
    if (p_index >= 0) {
        if (different_jobs[p_index].job_state != 2) {kill(foreground_pid[p_index], SIGINT);}
        else {int status; kill(different_jobs[p_index].pid, SIGKILL); usleep(200000);
            if (waitpid(different_jobs[p_index].pid, &status, WNOHANG) > 0) {foreground_pid[p_index] = -1; different_jobs[p_index] = empty_job;}
        }
    }
}

void run_foreground_job(char cmd_line[81][255], char*cmd_line_points[], char *command, char output[], size_t size) {
    getcwd(output, size);
    strcat(output, "/");
    strcat(output, command);
    cmd_line_points[0] = output;
    if (strcmp(cmd_line[0], "ls") == 0 && cmd_line[1][0] == '\0' && cmd_line[1][0] != '&') {cmd_line_points[1] = "."; reinsert_null(cmd_line_points);}
    int status; char temp[255] = "";
    create_name(temp, cmd_line); int i; int found = 0;
    for (i = 0; i < 6; ++i) {if (different_jobs[i].job_id < 0) {found = 1; break;}}
    if (found) {
        foreground_pid[i] = fork();
        different_jobs[i] = create_job(i + 1, foreground_pid[i], 0, temp, 1);
        if (foreground_pid[i] < 0) {
            printf("Unexpected error in forking\n");
            return;
        } else if (foreground_pid[i] == 0) {
            if (execv(cmd_line_points[0], cmd_line_points) < 0) {
                cmd_line_points[0] = cmd_line[0];
                if (execv(cmd_line_points[0], cmd_line_points)) {
                    if (execvp(cmd_line_points[0], cmd_line_points) < 0) {
                        printf("%s cannot be executed\n", cmd_line_points[0]);
                    }
                }
            }
            exit(1);
        } else {
            waitpid(foreground_pid[i], &status, WUNTRACED);
            if (!WIFSTOPPED(status)) {
                foreground_pid[i] = -1;
                different_jobs[i] = empty_job;
            }
        }
    }
    else printf("Cannot execute more than 6 jobs\n");
}

void run_background_job(char cmd_line[81][255], char * cmd_line_points[], char *command, char output[], size_t size) {
    getcwd(output, size);
    strcat(output, "/");
    strcat(output, command);
    cmd_line_points[0] = output;
    if (strcmp(cmd_line[0], "ls") == 0 && cmd_line[1][0] == '\0' && cmd_line[1][0] != '&') {cmd_line_points[1] = "."; reinsert_null(cmd_line_points);}
    int index = -1; int j;
    for (j = 0; j < 6; ++j) {if (foreground_pid[j] == -1 && different_jobs[j].is_foreground != 1) {
        char temp[255] = ""; create_name(temp, cmd_line);
        foreground_pid[j] = fork();
        different_jobs[j] = create_job(j + 1, foreground_pid[j], 1, temp, 0);
        index = j; break;}
    }
    if (index >= 0) {
        if (foreground_pid[index] < 0) {
            printf("Unexpected error in forking\n");
            return;
        } else if (foreground_pid[index] == 0) {
            setpgid(0, 0);
            if (execv(cmd_line_points[0], cmd_line_points) < 0) {
                cmd_line_points[0] = cmd_line[0];
                if (execv(cmd_line_points[0], cmd_line_points)) {
                    if (execvp(cmd_line_points[0], cmd_line_points) < 0) {
                        printf("%s cannot be executed\n", cmd_line_points[0]);
                    }
                }
            }
            exit(1);
        }
    }
    else {printf("Cannot start more than 6 background processes\n");}
 }

void parse_line(char cmd_line[81][255], char*cmd_line_points[], char input[]) {
    int i = 0;
    char * token = strtok(input, " \n");
    for (; token != NULL && i < 81; ++i) {
        strcpy(cmd_line[i], token);
        token = strtok(NULL, " \n");
    }
    for (i = 0; cmd_line[i][0] != '\0' && i < 81; ++i) {
        cmd_line_points[i] = cmd_line[i];
    }
}

int check_if_background(char cmd_line[81][255]) {
    for (int i = 0; cmd_line[i][0] != '\0'; ++i) {
        if (cmd_line[i][0] == '&') return 1;
    }
    return 0;
}

int look_for_index_of_sign(char cmd_line[81][255]) {
    int i = 0;
    for (; i < 81 && cmd_line[i][0] != '\0'; ++i) {}
    return i - 2;
}

void parse_input(char cmd_line[81][255], char*cmd_line_points[], char input[], int * is_true, char output[], size_t size) {
    parse_line(cmd_line, cmd_line_points, input);
    char *token = cmd_line[0];
    int index = look_for_index_of_sign(cmd_line); char *token2 = NULL;
    if (index >= 0) token2 = cmd_line[index];
    if (token == NULL) {
        printf("Error: No input detected.\n");
    }
    else if (token2 != NULL && strcmp(token2, ">") == 0) {
        output_redirection( cmd_line, input, is_true, output, size, index);
    }
    else if (token2 != NULL && strcmp(token2, "<") == 0) {
        input_redirection( cmd_line, input, is_true, output, size, index);
    }
    else if (token2 != NULL && strcmp(token2, ">>") == 0) {
        output2_redirection(cmd_line, input, is_true, output, size, index);
    }
    else if (strcmp(token, "cd") == 0) {
        cd(cmd_line, output, size);
    }
    else if (strcmp(token, "pwd") == 0) {
        pwd(output, size);
    }
    else if (strcmp(token, "jobs") == 0) {
        jobs();
    }
    else if (strcmp(token, "fg") == 0) {
        fg(cmd_line, output);
    }
    else if (strcmp(token, "bg") == 0) {
        bg(cmd_line, output);
    }
    else if (strcmp(token, "kill") == 0) {
        my_kill(cmd_line, output);
    }
    else if (strcmp(token, "quit") == 0) {
        quit(is_true);
    }
    else {
        if (check_if_background(cmd_line)) {run_background_job(cmd_line, cmd_line_points, token, output, size);}
        else {run_foreground_job(cmd_line, cmd_line_points, token, output, size);}
        }
}

int main() {
    char input[255];
    char output[255];
    char input_cpy[255];
    char cmd_line[81][255];
    char * cmd_line_points[81];
    size_t size = sizeof(output);
    initialize_jobs();
    signal(SIGINT, handle_sigint);
    signal(SIGCHLD, handle_sigchld);
    signal(SIGTSTP, handle_SIGTSTP);
    for (int is_true = 1; is_true != 0; ) {
        fill_with_defaults(cmd_line);
        usleep(100000);
        printf("prompt > ");
        fgets(input, sizeof(input), stdin);
        //if (feof(stdin)) exit(0);//needs testing. doesn't work
        strcpy(input_cpy, input);
        parse_input(cmd_line, cmd_line_points, input_cpy, &is_true, output, size);
    }
    return 0;
}

