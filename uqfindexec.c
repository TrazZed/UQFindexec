#include <csse2310a3.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <locale.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#define COMMAND_LINE_ERROR 20
#define INVALID_DIRECTORY 6
#define INVALID_COMMAND 8
#define FAILED_PROCESS 15
#define READ_END 0
#define WRITE_END 1
#define NO_REDIRECTION (-5)
#define SIGNAL_INTERRUPT 7

volatile bool signalInterrupt = false; // Did the program receieve a SIGINT

/**
 * A struct to store values from the command line arguments and boolean values
 * for whether certain parameters were parsed
 */
typedef struct {
    bool inputDirectory; // true if the user specified a --directory
    bool statistics; // true if the user specified --statistics
    bool parallel; // true if the user specified --parallel
    bool all; // true if the user specified --all
    bool descend; // true if the user specified --descend
    bool command; // true if the user specified a cmd
    char* directory; // the directory for the file, or default "."
    const char* cmd; // the command input by the user, or default "echo {}"
} CommandParameters;

/**
 * A structure that is used to store statistics values for the program
 */
typedef struct {
    int numSuccess; // Number of successful files
    int numSignals; // Number of files stopped due to signals
    int numFailed; // Number of files that failed
    int numNotExecuted; // Number of files that were not executed
} Statistics;

/**************************DECLARATIONS****************************************/
CommandParameters command_line_check(int argc, char** argv);
void directory_check(
        CommandParameters* params, int argc, char** argv, int argNum);
void statistics_check(CommandParameters* params);
void parallel_check(CommandParameters* params);
void all_check(CommandParameters* params);
void descend_check(CommandParameters* params);
void command_line_error();

void check_directory(char* directory);
void check_commands(CommandPipeline* parsed);

int scan_directory(struct dirent*** list, char* directory, bool all);
int ignore_subdirectories(const struct dirent* entry);
int ignore_hidden_and_subdirectories(const struct dirent* entry);

char* file_path(
        char* directory, bool inputDirectory, const struct dirent* entry);
char** generate_command_arguments(
        CommandPipeline* parsed, char* path, int* numArgs, int cmdNum);
char* file_substitution(char* string, char* path);

Statistics execute_pipelines(int numFiles, CommandParameters params,
        struct dirent** list, CommandPipeline* parsed, bool* failedProcess);

void execute_command(CommandPipeline* parsed, char* path, int cmdNum,
        int fileNum, pid_t pid[][parsed->numCmds - 1], int infd, int outfd,
        int fds[][2]);
void child_process(int cmdNum, int infd, int outfd, int numCmds, int fds[][2],
        char** args, char* path);
void parent_process(int cmdNum, int numCmds, int infd, int outfd, int fds[][2]);
void check_processes(int numCmds, int fileNum, pid_t pid[][numCmds - 1],
        bool* failedProcess, bool* notExecuted, bool* nonZeroExit,
        int* numNotExecuted, int* numSignals);
void check_processes_parallel(int numFiles, int numCmds,
        pid_t pid[][numCmds - 1], bool* failedProcess, Statistics* stats);

int redirect_stdin(char* stdinFileName, char* path);
int redirect_stdout(char* stdoutFileName, char* path);
char* std_file_substitution(char* stdFileName, char* path);

void print_statistics(Statistics stats);
void finalise(int numFiles, struct dirent** list, CommandPipeline* parsed,
        Statistics stats, CommandParameters params, bool failedProcess);

/******************************************************************************/

/**
 * A signal handler that sets the boolean flag signalInterrupt to true when
 * run, i.e. when the program recieves a SIGINT
 */
void signal_interruption()
{
    signalInterrupt = true;
}

/**
 * main()
 * --------------
 *  The main execution of the program that calls all required functions to
 *  perform all required actions.
 *
 *  int argc: the number of commandline arguments
 *  char** argv: the array of strings storing commandline arguments
 *
 *  Returns: 0 if the program does not exit at any time due to an error code
 */
int main(int argc, char** argv)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_interruption;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, 0);
    bool failedProcess = false;
    CommandParameters params = command_line_check(argc, argv);
    check_directory(params.directory);
    CommandPipeline* parsed = parse_pipeline_string(params.cmd);
    check_commands(parsed);
    struct dirent** list;
    int numFiles = scan_directory(&list, params.directory, params.all);
    Statistics stats
            = execute_pipelines(numFiles, params, list, parsed, &failedProcess);
    finalise(numFiles, list, parsed, stats, params, failedProcess);
    return 0;
}

/**
 * command_line_check()
 * ------------------------
 * Handles command line checking arguments and stores required values in
 * a Statistics structure
 *
 * int argc: the number of command line arguments
 * char** argv: the command line arguments
 *
 * Returns: A CommandParameters structure with filled associated values
 *
 */
CommandParameters command_line_check(int argc, char** argv)
{
    CommandParameters params
            = {false, false, false, false, false, false, ".", "echo {}"};
    // Boolean to check if we should be at the end of the argument list
    bool atEnd = false;
    for (int i = 1; i < argc; i++) {
        // If there are arguments after our command, error
        if (atEnd) {
            command_line_error();
        }
        if (strcmp(argv[i], "--directory") == 0) {
            directory_check(&params, argc, argv, i);
            i++;
        } else if (strcmp(argv[i], "--statistics") == 0) {
            statistics_check(&params);
        } else if (strcmp(argv[i], "--parallel") == 0) {
            parallel_check(&params);
        } else if (strcmp(argv[i], "--all") == 0) {
            all_check(&params);
        } else if (strcmp(argv[i], "--descend") == 0) {
            descend_check(&params);
        } else {
            // Check that the command is valid
            if ((argv[i][0] == '-') && (argv[i][1] == '-')) {
                command_line_error();
            }
            if (!params.command) {
                // Check we don't have an empty string for a command
                if (strcmp(argv[i], "") == 0) {
                    command_line_error();
                }
                params.cmd = argv[i];
                params.command = true;
                atEnd = true;
            }
        }
    }
    return params;
}

/**
 * directory_check()
 * --------------------
 * A helper function to check validty if the user passed --directory into the
 * command line arguments
 *
 * CommandParameters* params: a pointer to the struct storing information
 * regarding the command line parameters
 * int argc: the number of arguments passed into the commandline
 * char** argv: a list of strings that store the command line arguments
 * int argNum: the current argument number being processed
 */
void directory_check(
        CommandParameters* params, int argc, char** argv, int argNum)
{
    // If already found a dictionary, error
    if (!params->inputDirectory) {
        // Ensure a dictionary path is specified, and that it isn't
        // a command or the empty string
        if (argNum + 1 == argc) {
            command_line_error();
        } else if (strcmp(argv[argNum + 1], "") == 0) {
            command_line_error();
        }
        params->directory = argv[argNum + 1];
        params->inputDirectory = true;
    } else {
        command_line_error();
    }
}

/**
 * statistics_check()
 * -----------------------
 *  A helper function to check validity of the --statistics option
 *
 * CommandParameters* params: a pointer to the struct storing information
 * regarding the command line parameters
 */
void statistics_check(CommandParameters* params)
{
    // If already specified, error
    if (!params->statistics) {
        params->statistics = true;
    } else {
        command_line_error();
    }
}

/**
 * parallel_check()
 * -------------------
 *  A helper function to check validity of the --parallel option
 *
 * CommandParameters* params: a pointer to the struct storing information
 * regarding the command line parameters
 */
void parallel_check(CommandParameters* params)
{
    // If already specified, error
    if (!params->parallel) {
        params->parallel = true;
    } else {
        command_line_error();
    }
}

/**
 * all_check()
 * ----------------
 *  A helper function to check validity of the --all option
 *
 * CommandParameters* params: a pointer to the struct storing information
 * regarding the command line parameters
 */
void all_check(CommandParameters* params)
{

    // If already specified, error
    if (!params->all) {
        params->all = true;
    } else {
        command_line_error();
    }
}

/**
 * descend_check()
 * --------------------
 *  A helper function to check validity of the --descend option
 *
 * CommandParameters* params: a pointer to the struct storing information
 * regarding the command line parameters
 */
void descend_check(CommandParameters* params)
{
    // If already specified, error
    if (!params->descend) {
        params->descend = true;
    } else {
        command_line_error();
    }
}

/**
 * command_line_error()
 * ----------------------
 *  If an error occurs in the command line checking, print the appropriate
 *  message to stderr and exit with the correct code
 */
void command_line_error()
{
    fprintf(stderr,
            "Usage: uqfindexec [--directory dir] [--statistics] "
            "[--parallel] "
            "[--all] [--descend] [cmd]\n");
    exit(COMMAND_LINE_ERROR);
}

/**
 * check_directory()
 * ----------------------
 * A function to check if the specified directory is valid, and if not
 * print the appropriate error message and exit with the appropriate code
 *
 * char* directory: the directory to check is valid
 *
 * REF: This function and its properties (including NULL on error) were
 * taken from the opendir man page
 */
void check_directory(char* directory)
{
    DIR* direct = opendir(directory);
    if (direct == NULL) {
        fprintf(stderr, "uqfindexec: cannot read directory \"%s\"\n",
                directory);
        exit(INVALID_DIRECTORY);
    }
    closedir(direct);
}

/**
 * check_commands()
 * ----------------------
 * A function to check if the parsed commands are valid or not, and if not,
 * print the appropraite error message and exit with the appropriate code
 *
 * CommandPipeline* parsed: the parsed command lines structure
 *
 * REF: Information taken from the parse_pipeline_string man page
 */
void check_commands(CommandPipeline* parsed)
{
    if (parsed == NULL) {
        fprintf(stderr, "uqfindexec: invalid command\n");
        exit(INVALID_COMMAND);
    }
}

/**
 * scan_directory()
 * -----------------------
 * Helper function to return all the files in the directory to the supplied
 * dirent strucutre list, and ignoring hidden files unless all is specified
 * true, and returns an integer of the number of files in the directory
 *
 * struct dirent*** list: a pointer to the list of files in the directory
 * char* directory: the directory to scan for files
 * bool all: true if --all was passed, so search for hidden files
 *
 * Return: An integer of the number of files found in the directory
 *
 * REF: Information regarding scandir retrieved from man page
 */
int scan_directory(struct dirent*** list, char* directory, bool all)
{
    // Determine all the files in the directory
    int numFiles;
    // If the --all parameter has been parsed, include the hidden files, else
    // filter them out
    setlocale(LC_COLLATE, "en_AU");
    if (all) {
        numFiles = scandir(directory, list, ignore_subdirectories, alphasort);
    } else {
        numFiles = scandir(
                directory, list, ignore_hidden_and_subdirectories, alphasort);
    }
    return numFiles;
}

/**
 * ignore_hidden_and_subdirectories()
 * -------------------------------------------
 * Helper function to filter hidden files and subdirectories, returns 0 if
 * the file is hidden or subdirectory, or 1 if the file is not hidden
 *
 * const struct dirent* entry: the file to check
 *
 * Returns: 1 if it is a regular file and not hidden, 0 if not
 *
 * REF: Properties regarding dirent* entry retrieved from man page
 */
int ignore_hidden_and_subdirectories(const struct dirent* entry)
{
    return (((entry->d_type == DT_REG) || (entry->d_type == DT_LNK))
            && entry->d_name[0] != '.');
}

/**
 * ignore_subdirectories()
 * ----------------------------
 * Helper function to filter subdirectories, returns 0 if the file is a
 * subdirectory, or 1 if it isnt
 *
 * const struct dirent* entry: the file to check
 *
 * Returns: 1 if it is a regular file, including hidden, and 0 if not
 *
 * REF: Properties regarding dirent* entry retrieved from man page
 */
int ignore_subdirectories(const struct dirent* entry)
{
    return ((entry->d_type == DT_REG) || (entry->d_type == DT_LNK));
}

/**
 * file_path()
 * ------------------------
 * Given a file name from a dirent entry, return a string that details the
 * path to the directory. Require the boolean inputDirectory to format
 * accordingly if the user input a specific directory
 *
 * char* directory: the directory that the file is in
 * char* inputDirectory: true if the user specified a directory
 * const struct dirent* entry: the entry to find the path for
 *
 * Returns: a string detailing the path to the file
 */
char* file_path(
        char* directory, bool inputDirectory, const struct dirent* entry)
{
    // If the file is in the current directory
    if (!inputDirectory) {
        char* path = malloc(sizeof(char) * (strlen(entry->d_name) + 1));
        strcpy(path, entry->d_name);
        return path;
    }
    // Else the file is not in the current directory, check if the format
    // has a slash at the end, and if so or not, append accordingly
    if (directory[strlen(directory) - 1] == '/') {
        char* path = malloc(
                sizeof(char) * (strlen(directory) + strlen(entry->d_name) + 1));
        strcpy(path, directory);
        strcat(path, entry->d_name);
        return path;
    }
    char* path = malloc(
            sizeof(char) * (strlen(directory) + strlen(entry->d_name) + 2));
    strcpy(path, directory);
    strcat(path, "/");
    strcat(path, entry->d_name);
    return path;
}

/**
 * file_substitution()
 * -------------------------
 *  A helper fuunction that performs file substitution when necessary. Given a
 *  string, e.g. "this is {}" and a target string, e.g. "target", will
 *  substitute the target string into the original string whenever {} is
 *  detected, e.g. "this is target"
 *
 *  char* string: the string to perform file substitution on
 *  char* path: the path to substitute in the placeholders {}
 *
 *  Returns: a string of the original string with the file substitution
 *  performed, dynamically allocated memory
 *
 */
char* file_substitution(char* string, char* path)
{
    char* substituted = calloc(sizeof(char) + 1, sizeof(char));
    for (int i = 0; i < (int)strlen(string); i++) {
        if ((string[i] == '{') && (string[i + 1] == '}')) {
            substituted = realloc(substituted,
                    sizeof(char) * (strlen(substituted) + strlen(path) + 2));
            strcat(substituted, path);
            i++;
            i++;
        }
        substituted = realloc(
                substituted, sizeof(char) * (strlen(substituted) + 2));
        strncat(substituted, &string[i], 1);
    }
    return substituted;
}

/**
 * generate_command_arguments()
 * --------------------------------------
 * Given the parsed command line arguments and the path to the file, create an
 * array of strings that can be passed into execvp alongside the command to
 * execute on the file
 *
 * CommandPipeline* parsed: the parsed command arguments from the command line
 * int* numArgs: a pointer to an integer representing the number of arguments
 * int cmdNum: an integer representing the command number we are generating
 * arguments for
 *
 * Returns: a list of strings detailing the command arguments to passed into
 * execvp
 */
char** generate_command_arguments(
        CommandPipeline* parsed, char* path, int* numArgs, int cmdNum)
{
    // Allocate memory for an array of strings to store the command,
    // filename and arguments
    int count = 1;
    char** args = malloc(sizeof(char*) * count);
    // Allocate memory for each string
    args[0] = file_substitution(parsed->cmdArray[cmdNum][0], path);
    // For each command line argument, keep re-allocating more memory
    // and
    //  adding them to the new list of arguments
    while (1) {
        // If NULL we are at end, exit the loop
        if (parsed->cmdArray[cmdNum][count] == NULL) {
            break;
        }
        // Standard argument
        args = realloc(args, sizeof(char*) * (count + 1));
        args[count] = file_substitution(parsed->cmdArray[cmdNum][count], path);

        count++;
    }
    args = realloc(args, sizeof(char*) * (count + 1));
    args[count] = NULL;
    count++;
    *numArgs = count;
    return args;
}

/**
 * execute_pipelines()
 * ------------------------
 *  Executes the pipelines of commands for each file in the directory, and
 *  calls necessary functions to complete this process as detailed.
 *
 *  int numFiles: the number of files in the directory
 *  ComandParameters params: a struct detailing all information regarding the
 *  parameters passed in the command line
 *  struct dirent** list: a list of files in the directory
 *  CommandPipeline* parsed: the parsed command from the commandline
 *  bool* failedProcess: a pointer to a boolean detailing if the code ran into
 *  at least one failed process
 *
 *  Return: a Statistics structure detailing statistics about the execution of
 *  pipelines on the files
 */
Statistics execute_pipelines(int numFiles, CommandParameters params,
        struct dirent** list, CommandPipeline* parsed, bool* failedProcess)
{
    Statistics stats = {0, 0, 0, 0};
    pid_t pid[numFiles - 1][parsed->numCmds - 1]; // Create 2D array of pid's
    for (int i = 0; i < numFiles; i++) {
        // Bools to track if we fail a command or get a FAILED_PROCESS exit code
        bool nonZeroExit = false, notExecuted = false;
        int fds[parsed->numCmds - 1][2]; // Create pipes
        for (int j = 0; j < parsed->numCmds; j++) {
            pipe(fds[j]); // Set up pipes
        }
        char* path
                = file_path(params.directory, params.inputDirectory, list[i]);
        int outfd = redirect_stdout(parsed->stdoutFileName, path);
        int infd = redirect_stdin(parsed->stdinFileName, path);
        if (outfd == -1 || infd == -1) {
            *failedProcess = true;
            (stats.numNotExecuted)++;
            continue;
        }
        for (int cmdNum = 0; cmdNum < parsed->numCmds; cmdNum++) {
            execute_command(parsed, path, cmdNum, i, pid, infd, outfd, fds);
        }
        if (!params.parallel) {
            check_processes(parsed->numCmds, i, pid, failedProcess,
                    &notExecuted, &nonZeroExit, &stats.numNotExecuted,
                    &stats.numSignals);
            // If a process exited with non-zero code, and the process didnt
            // fail, increase the number of non-zero exit commands counter
            if (nonZeroExit && !notExecuted) {
                (stats.numFailed)++;
                // Else if the code was executed, increase success
            } else if (!notExecuted) {
                (stats.numSuccess)++;
            }
        }
        free(path);
        // If we received SIGINT, we stop processing files
        if (signalInterrupt && !params.parallel) {
            // If we were on the last file, ignore so we exit with correct code
            if (i == numFiles - 1) {
                signalInterrupt = false;
            }
            break;
        }
    }
    if (params.parallel) {
        check_processes_parallel(
                numFiles, parsed->numCmds, pid, failedProcess, &stats);
    }
    return stats;
}

/**
 * execute_command()
 * --------------------
 *  A helper function that will execute a command on a file. Creates a child
 *  process and uses pipelining for redirection of inputs and outputs.
 *
 *  CommandPipeline* parsed: the struct storign properties of the parsed command
 *  from the commandline
 *  char* path: the path from the directory to the file
 *  int cmdNum: the command number beinge executed
 *  int fileNum: the file number to execute the command on
 *  pid_t pid[][parsed->numCmds-1]: the array storing all the pids for the
 *  processes
 *  int infd: the file descriptor for redirection of STDIN
 *  int outfd: the file descriptor for redirection of STDOUT
 *  int fds[][2]: the 2D array storing fds for pipelining
 *
 */
void execute_command(CommandPipeline* parsed, char* path, int cmdNum,
        int fileNum, pid_t pid[][parsed->numCmds - 1], int infd, int outfd,
        int fds[][2])
{
    fflush(stdout);
    int count;
    char** args = generate_command_arguments(parsed, path, &count, cmdNum);
    pid[fileNum][cmdNum] = fork();
    if (!pid[fileNum][cmdNum]) {
        child_process(cmdNum, infd, outfd, parsed->numCmds, fds, args, path);
    } else {
        parent_process(cmdNum, parsed->numCmds, infd, outfd, fds);
    }
    // Free all the memory
    for (int j = 0; j < count; j++) {
        free(args[j]);
    }
    free(args);
}

/**
 * child_process()
 * -------------------
 * A function that will execute the process for the child function after
 * forking, dependent on the cmdNum we are on, passing the redirection file
 * descriptors infd and outfd, the total number of commands in the pipeline
 * numCmds, the 2-d array of file descriptors for pipes fds, the arguments for
 * the execvp command args, the path of the file.
 *
 * int cmdNum: the command number to execute
 * int infd: the file descriptor for redirecting STDIN
 * int outfd: the file descriptor for redirecting STDOUT
 * int numCmds: the number of commands in the process
 * int fds[][2]: the 2D array of file descriptors for piping
 * char** args: the command arguments to pass to execvp
 * char* path: the path to the file
 */
void child_process(int cmdNum, int infd, int outfd, int numCmds, int fds[][2],
        char** args, char* path)
{
    // If at first command, read from stdin
    if (cmdNum == 0) {
        if (infd != NO_REDIRECTION) {
            dup2(infd, STDIN_FILENO);
            close(infd);
        }
    }
    // If at last command, output to stdout
    if (cmdNum == numCmds - 1) {
        if (outfd != NO_REDIRECTION) {
            dup2(outfd, STDOUT_FILENO);
            close(outfd);
        }
    }
    // If not the first command, read input from the previous
    if (cmdNum > 0) {
        dup2(fds[cmdNum - 1][READ_END], STDIN_FILENO);
        close(fds[cmdNum - 1][READ_END]);
    }
    // If not the last command, write to the write end of the pipe
    if (cmdNum < numCmds - 1) {
        dup2(fds[cmdNum][WRITE_END], STDOUT_FILENO);
        close(fds[cmdNum][WRITE_END]);
    }
    execvp(args[0], args);
    // If the program returns, the execution failed
    fprintf(stderr,
            "uqfindexec: unable to execute \"%s\" when processing "
            "\"%s\"\n",
            args[0], path);
    exit(FAILED_PROCESS);
}

/** parent_process()
 * ------------------------
 * A helper function to execute the parent process after forking, closing all
 * file descriptors when necessary. Requires the command number cmdNum, the
 * number of commands numCmds, the file descriptors for file redirection infd
 * and outfd, and the 2-d array of file descriptors for the pipes
 *
 * int cmdNum: the command number currently being executed
 * int numCmds: the total number of commands to execute
 * int infd: the file descriptor for redirecting STDIN
 * int outdf: the file descriptor for redirecting STDOUT
 * int fds[][2]: the 2D array of fds for piping
 */
void parent_process(int cmdNum, int numCmds, int infd, int outfd, int fds[][2])
{
    fflush(stdout);
    if (cmdNum == 0) {
        if (infd != NO_REDIRECTION) {
            close(infd);
        }
    }
    if (cmdNum == numCmds - 1) {
        if (outfd != NO_REDIRECTION) {
            close(outfd);
        }
        close(fds[cmdNum][WRITE_END]);
        close(fds[cmdNum][READ_END]);
    }
    if (cmdNum > 0) {
        close(fds[cmdNum - 1][READ_END]);
    }
    if (cmdNum < numCmds - 1) {
        close(fds[cmdNum][WRITE_END]);
    }
}

/**
 * check_process()
 * -------------------------
 * A helper function to check the status of child processes and perform
 * calculations as expected. Requires the number of commands numCmds, the
 * array of pids pid, and pointers to boolean variables failedProcess,
 * notExecuted, nonZeroExit, and integer pointers to numNotExecuted and
 * numSignals for statistics processing
 *
 * int numCmds: the number of commands to process
 * int fileNum: the file number that the processes executed on
 * pid_t pid[][numCmds-1]: the 2D array for storing program id's
 * bool* failedProcess: a pointer to a boolean to indicate if at least one
 * process failed bool* notExecuted: a pointer to a boolean that indicates if a
 * file pipeline wasn't executed bool* nonZeroExit: a pointer to a boolean that
 * indicates if a file pipeline had a non-zero exit code int* numNotExecuted: a
 * pointer to an integer that counts the number of pipelines that weren't
 * executed int* numSignals: a pointer to an integer that counts the number of
 * pipelines that weren't processed due to a signal
 */
void check_processes(int numCmds, int fileNum, pid_t pid[][numCmds - 1],
        bool* failedProcess, bool* notExecuted, bool* nonZeroExit,
        int* numNotExecuted, int* numSignals)
{
    for (int cmdNum = 0; cmdNum < numCmds; cmdNum++) {
        int status;
        // Wait for the child to process to see the status
        waitpid(pid[fileNum][cmdNum], &status, 0);
        if (WIFEXITED(status)) {
            int exitCode = WEXITSTATUS(status);
            // If the process failed, set a flag to exit with the
            // correct code
            if (exitCode == FAILED_PROCESS) {
                (*numNotExecuted)++;
                *failedProcess = true;
                *notExecuted = true;
            } else if (exitCode != 0) {
                *nonZeroExit = true;
            }
        } else if (WIFSIGNALED(status)) {
            *notExecuted = true;
            (*numSignals)++;
        }
    }
}

/** check_processes_parallel()
 * --------------------------------
 * If the user specifies --parallel, all children must be reaped after all
 * files have been processed. Requires the numFiles, numCmds, the 2D array
 * of pids pid, and pointer pointing to failedProcess, numNotExecuted,
 * numSignals, numFailed and numSuccess)
 *
 * int numFiles: the number of files in the directory to be processed
 * int numCmds: the number of commands in the pipeline
 * pid_t pid[][numCmds-1]: a 2D array storing all the process id's
 * bool* failedProcess: a pointer to a boolean, true if a process has failed
 * Statistics* stats: a pointer to a Statistics struct that records the data
 * for --statistics
 */
void check_processes_parallel(int numFiles, int numCmds,
        pid_t pid[][numCmds - 1], bool* failedProcess, Statistics* stats)
{
    for (int i = 0; i < numFiles; i++) {
        bool nonZeroExit = false, notExecuted = false;
        for (int cmdNum = 0; cmdNum < numCmds; cmdNum++) {
            int status;
            // Wait for the child to process to see the status
            waitpid(pid[i][cmdNum], &status, 0);
            if (WIFEXITED(status)) {
                int exitCode = WEXITSTATUS(status);
                // If the process failed, set a flag to exit with the
                // correct code
                if (exitCode == FAILED_PROCESS) {
                    (stats->numNotExecuted)++;
                    *failedProcess = true;
                    notExecuted = true;
                } else if (exitCode != 0) {
                    nonZeroExit = true;
                }
            } else if (WIFSIGNALED(status)) {
                notExecuted = true;
                (stats->numSignals)++;
            }
        }
        if (nonZeroExit && !notExecuted) {
            // Else if the code was executed, increase success
            (stats->numFailed)++;
            // Else if the code was executed, increase success
        } else if (!notExecuted) {
            (stats->numSuccess)++;
        }
    }
}

/** redirect_stdin()
 * ----------------------
 * A helper function that generates a fd to redirect the stdin to a file on the
 * given path. Returns the fd of the new input if successful, -1 if an error
 * occurs, or NO_REDIRECTION if no input redirection specified
 *
 * char* stdinFileName: the name of the file to redirect STDIN
 * char* path: the path of the file to redirect
 *
 * Return: an fd for the STDIN redirection, or NO_REDIRECTION if none was
 * specified
 */
int redirect_stdin(char* stdinFileName, char* path)
{
    char* stdinName = std_file_substitution(stdinFileName, path);
    if (stdinName != NULL) {
        int infd = open(stdinName, O_RDONLY);
        // If an error occurs, exit appropriately
        if (infd < 0) {
            fprintf(stderr,
                    "uqfindexec: cannot read \"%s\" when processing "
                    "\"%s\"\n",
                    stdinName, path);
        }
        free(stdinName);
        return infd;
    }
    return NO_REDIRECTION;
}

/** redirect_stdout()
 * ----------------------
 * A helper function that generates a fd to redirect the stdout to a file on the
 * given path. Returns the fd of the new input if successful, -1 if an error
 * occurs, or NO_REDIRECTION if no input redirection specified
 *
 * char* stdoutFileName: the name of the file to redirect STDOUT
 * char* path: the path of the file to redirect
 *
 * Return: an fd for the STDOUT redirection, or NO_REDIRECTION if none was
 * specified
 */
int redirect_stdout(char* stdoutFileName, char* path)
{
    char* stdoutName = std_file_substitution(stdoutFileName, path);
    if (stdoutName != NULL) {
        int outfd = open(stdoutName, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
        if (outfd < 0) {
            fprintf(stderr,
                    "uqfindexec: unable to open \"%s\" for writing "
                    "while processing \"%s\"\n",
                    stdoutName, path);
        }
        free(stdoutName);
        return outfd;
    }
    return NO_REDIRECTION;
}

/** std_file_substitution()
 * ------------------------------
 * A helper function to deal with the string for the file redirection to
 * stdFileName, on the path if it is a placeholder, and perform the correct
 * substitution, and return stdName for the name of the file, otherwise return
 * NULL if a file wasnt specified
 *
 * char* stdFileName: the name of the file to redirect to, could be {}
 * char* path: the path from the current directory to the file
 *
 * Return: a string of the filename, including the path, or NULL if none was
 * specified
 */
char* std_file_substitution(char* stdFileName, char* path)
{
    if (stdFileName != NULL) {
        char* stdName = file_substitution(stdFileName, path);
        return stdName;
    }
    return NULL;
}

/** print_statistics()
 * ---------------------
 * A function to print out the statistics for the process, including the
 * total number of files numFiles, the numSuccess, the numFailed, the number of
 * files interrupted by a signal numSignals, and the number of not executed
 * pipelines numNotExecuted
 *
 * Statistics stats: a struct detailing all the statistics from file processing
 */
void print_statistics(Statistics stats)
{
    int numFiles = stats.numSuccess + stats.numFailed + stats.numSignals
            + stats.numNotExecuted;
    fprintf(stderr, "Attempted to operate on %d files\n", numFiles);
    fprintf(stderr, " - processing succeeded for %d files\n", stats.numSuccess);
    fprintf(stderr,
            " - non-zero exit status detected when processing %d files\n",
            stats.numFailed);
    fprintf(stderr, " - processing was terminated by signal for %d files\n",
            stats.numSignals);
    fprintf(stderr, " - pipeline not executed for %d files\n",
            stats.numNotExecuted);
}

/** finalise()
 * ----------------
 * Finalise code execution, freeing memory from list, parsed, and performing
 * statistics printing with numFailed, numSignals, numNotExecuted, numSuccess if
 * statistics boolean is true, and exit with the correct code is failedProcess
 * is true
 *
 * int numFiles: the number of files to be processed
 * struct dirent** list: a list of all files in the directory to process
 * CommandPipeline* parsed: a struct storing information on the command
 * information from the command line arguments
 * Statistics stats: the statistics recorded from the file pipeline executions
 * CommandParameters params: the struct storing all information regarding
 * commandline parameters
 * bool failedProcess: true if at least one process failed
 */
void finalise(int numFiles, struct dirent** list, CommandPipeline* parsed,
        Statistics stats, CommandParameters params, bool failedProcess)
{
    // Free all the memory
    for (int i = 0; i < numFiles; i++) {
        free(list[i]);
    }
    fflush(stdout);
    free(list);
    free_pipeline(parsed);
    if (params.statistics) {
        print_statistics(stats);
    }
    if (signalInterrupt && !params.parallel) {
        exit(SIGNAL_INTERRUPT);
    }
    if (failedProcess) {
        exit(FAILED_PROCESS);
    }
}
