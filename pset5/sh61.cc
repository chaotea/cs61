#include "sh61.hh"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>

// For the love of God
#undef exit
#define exit __DO_NOT_CALL_EXIT__READ_PROBLEM_SET_DESCRIPTION__

#define PIPE_FIRST  0
#define PIPE_AND    1
#define PIPE_OR     2


volatile sig_atomic_t interrupt_flag = 0;
bool pipeline_in_background = false;

struct command {
    pid_t pid = -1;                  // process ID running this command, -1 if none
    std::vector<std::string> args;   // list of arguments
    std::string input_file;          // redirected input file
    std::string output_file;         // redirected output file
    std::string error_file;          // redirected error file
    int read_pipe = -1;              // the fd of the read end of the pipe to this command
    command* next_command = nullptr; // pointer to the next command in the pipeline

    command();
    ~command();

    pid_t run(pid_t pgid);
};

command::command() {}
command::~command() {}

struct pipeline {
    command* command_child = nullptr;  // pointer to the first command in this pipeline
    pipeline* next_pipeline = nullptr; // pointer to the next pipeline in the conditional
    int and_or = PIPE_FIRST;           // the operator (&&, ||, or none) preceding the pipeline
};

struct conditional {
    pipeline* pipeline_child = nullptr;      // pointer to the first pipeline in this conditional
    conditional* next_conditional = nullptr; // pointer to the next conditional
    bool is_background = false;              // track whether this is a background conditional
};


// COMMAND EXECUTION

// command::run()
//    Create a single child process running the command in `this`.
//    Sets `this->pid` to the pid of the child process and returns `this->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//       This will require creating an array of `char*` arguments using
//       `this->args[N].c_str()`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: Update the `command` structure and this function to support
//       setting the child process’s process group. To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t command::run(pid_t pgid) {
    assert(this->args.size() > 0);

    // Create the array of arguments
    char* argv[this->args.size() + 1];
    for (size_t i = 0; i < this->args.size(); i++) {
        argv[i] = (char*) this->args[i].c_str();
    }
    argv[this->args.size()] = nullptr;

    // If the command is on the left side of a pipe, set up a new pipeline
    int pfd[2] = {-1, -1};
    if (this->next_command != nullptr) {
        int status = pipe(pfd);
        assert(status == 0);
    }

    // Initiate the fork
    pid_t p = fork();
    assert(p >= 0);
    if (p == 0) {
        // If it's the child process
        setpgid(p, pgid);

        // Connect to the read end of the preceding pipe (if it exists)
        if (this->read_pipe >= 0){
            dup2(this->read_pipe, STDIN_FILENO);
            close(this->read_pipe);
        }

        // Connect to the write end of the following pipe (if it exists)
        if (pfd[0] >= 0) {
            dup2(pfd[1], STDOUT_FILENO);
            close(pfd[0]);
            close(pfd[1]);
        }

        // Handle redirections
        const char* filename;
        if (!this->input_file.empty()) {
            filename = this->input_file.c_str();
            int input_fd = open(filename, O_RDONLY);
            if (input_fd < 0) {
                fprintf(stderr, "%s: No such file or directory ", filename);
                _exit(1);
            }
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (!this->output_file.empty()) {
            filename = this->output_file.c_str();
            int output_fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
            if (output_fd < 0) {
                fprintf(stderr, "%s: No such file or directory ", filename);
                _exit(1);
            }
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        if (!this->error_file.empty()) {
            filename = this->error_file.c_str();
            int error_fd = open(filename, O_CREAT|O_WRONLY|O_TRUNC, 0666);
            if (error_fd < 0) {
                fprintf(stderr, "%s: No such file or directory ", filename);
                _exit(1);
            }
            dup2(error_fd, STDERR_FILENO);
            close(error_fd);
        }

        // Execute the command or change directories
        if (strcmp(argv[0], "cd") == 0) {
            int status = chdir(argv[1]);
            if (status < 0) {
                _exit(1);
            }
        } else {
            int status = execvp(argv[0], argv);
            if (status < 0) {
                _exit(1);
            }
        }

        // Exit
        _exit(0);

    } else {
        // If it's the parent process
        setpgid(p, pgid);

        // Close the write end of the pipe and pass on the read end
        if (pfd[0] >= 0) {
            close(pfd[1]);
            if (this->next_command != nullptr) {
                this->next_command->read_pipe = pfd[0];
            }
        }

        // Close the read end of the pipe
        if (this->read_pipe >= 0) {
            close(this->read_pipe);
        }

        // Change directories
        if (strcmp(argv[0], "cd") == 0) {
            int status = chdir(argv[1]);
            if (status < 0) {
                return -1;
            }
        }
        
        // Set this->pid to the pid of the child process and return
        this->pid = p;
        return this->pid;
    }
}


// run_list(c)
//    Run the command *list* starting at `c`. Initially this just calls
//    `c->run()` and `waitpid`; you’ll extend it to handle command lists,
//    conditionals, and pipelines.
//
//    It is possible, and not too ugly, to handle lists, conditionals,
//    *and* pipelines entirely within `run_list`, but many students choose
//    to introduce `run_conditional` and `run_pipeline` functions that
//    are called by `run_list`. It’s up to you.
//
//    PART 1: Start the single command `c` with `c->run()`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in `command::run` (or in helper functions).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Make sure every process in the pipeline has the same
//         process group, `pgid`.
//       - Call `claim_foreground(pgid)` before waiting for the pipeline.
//       - Call `claim_foreground(0)` once the pipeline is complete.

int run_command(command* c) {
    // Run the command
    pid_t p = c->run(0);

    // Claim the foreground if it's the first command in a foreground pipeline
    if (c->read_pipe == -1 && !pipeline_in_background) {
        claim_foreground(p);
    }

    // If it's the last command in the pipeline, wait for the exit status
    int status = -1;
    if (c->next_command == nullptr) {
        int wstatus = waitpid(p, &status, 0);
        assert(wstatus >= 0);
    }

    // Return the foreground to the shell and return
    claim_foreground(0);
    return status;
}


int run_pipeline(pipeline* p, int status) {
    command* c = p->command_child;

    // Run the commands in the pipeline if:
    // 1. it is the first command
    // 2. it follows && and the previous command exited with status 0
    // 3. it follows || and the previous command didn't exit or exited with status != 0
    if ((p->and_or == PIPE_FIRST) ||
        (p->and_or == PIPE_AND && WIFEXITED(status) && WEXITSTATUS(status) == 0) ||
        (p->and_or == PIPE_OR && (!WIFEXITED(status) || WEXITSTATUS(status) != 0))) {
        while (c != nullptr) {
            status = run_command(c);
            c = c->next_command;
        }
    }

    // Return the exit status of the last command in the pipeline
    return status;
}


void run_conditional(conditional* c) {
    // Run the list of conditionals
    int status;

    while (c != nullptr) {
        // Run the pipelines in the current conditional
        pipeline* p = c->pipeline_child;
        pipeline_in_background = c->is_background;
        status = 0;

        if (c->is_background) {
            // If this is a background conditional, fork another
            // process to run pipelines in the background
            pid_t f = fork();
            if (f == 0) {
                while (p != nullptr) {
                    status = run_pipeline(p, status);
                    p = p->next_pipeline;
                }
                _exit(0);
            } else if (f < 0) {
                _exit(1);
            }
        } else {
            // Otherwise, run the pipelines in the foreground
            while (p != nullptr) {
                status = run_pipeline(p, status);
                p = p->next_pipeline;
            }
        }

        c = c->next_conditional;
    }
}


// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.

conditional* parse_line(const char* s) {
    shell_parser parser(s);

    conditional* head = nullptr;        // head of the conditional linked list
    conditional* cond = nullptr;        // the current conditional being parsed
    conditional* prev_cond = nullptr;   // the previous conditional
    pipeline* pipe = nullptr;           // the current pipeline being parsed
    pipeline* prev_pipe = nullptr;      // the previous pipeline
    command* cmd = nullptr;             // the current command being parsed
    command* prev_cmd = nullptr;        // the previous command

    for (shell_token_iterator it = parser.begin(); it != parser.end(); ++it) {
        switch (it.type()) {
        case TYPE_NORMAL:
            // Initialize a new conditional if needed
            // Parse the next argument in the command
            if (cond == nullptr) {
                cond = new conditional;
                pipe = new pipeline;
                cmd = new command;
                cond->pipeline_child = pipe;
                pipe->command_child = cmd;
                if (prev_cond != nullptr) {
                    prev_cond->next_conditional = cond;
                } else {
                    head = cond;
                }
            }
            cmd->args.push_back(it.str());
            break;
        case TYPE_SEQUENCE:
            // Finish parsing the current conditional
            prev_cond = cond;
            cond = nullptr;
            break;
        case TYPE_BACKGROUND:
            // Finish parsing the current conditional
            // and put it in the background
            cond->is_background = true;
            prev_cond = cond;
            cond = nullptr;
            break;
        case TYPE_AND:
            // Create a new pipeline and && it to the previous one
            prev_pipe = pipe;
            pipe = new pipeline;
            cmd = new command;
            pipe->command_child = cmd;
            pipe->and_or = PIPE_AND;
            prev_pipe->next_pipeline = pipe;
            break;
        case TYPE_OR:
            // Create a new pipeline and || it to the previous one
            prev_pipe = pipe;
            pipe = new pipeline;
            cmd = new command;
            pipe->command_child = cmd;
            pipe->and_or = PIPE_OR;
            prev_pipe->next_pipeline = pipe;
            break;
        case TYPE_PIPE:
            // Parse the next command in the pipeline
            prev_cmd = cmd;
            cmd = new command;
            prev_cmd->next_command = cmd;
            break;
        case TYPE_REDIRECT_OP:
            // Parse redirections and set the command's file descriptors
            std::string op = it.str();
            ++it;
            if (op.compare("<") == 0) {
                cmd->input_file = it.str();
            } else if (op.compare(">") == 0) {
                cmd->output_file = it.str();
            } else if (op.compare("2>") == 0) {
                cmd->error_file = it.str();
            }
            break;
        }
    }
    return head;
}


// Signal handler for interrupts
void interrupt_handler(int signum) {
    (void) signum;
    interrupt_flag = 1;
}


int main(int argc, char* argv[]) {
    FILE* command_file = stdin;
    bool quiet = false;

    // Check for `-q` option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = true;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            return 1;
        }
    }

    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back into the foreground
    // - Handle SIGINT signals to the shell via interrupt_handler
    claim_foreground(0);
    set_signal_handler(SIGTTOU, SIG_IGN);
    set_signal_handler(SIGINT, interrupt_handler);

    char buf[BUFSIZ];
    int bufpos = 0;
    bool needprompt = true;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("sh61[%d]$ ", getpid());
            fflush(stdout);
            needprompt = false;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == nullptr) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file)) {
                    perror("sh61");
                }
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            // Process the command line by conditional
            if (conditional* cond = parse_line(buf)) {
                // Run the conditional
                run_conditional(cond);
                // Cleanup the conditional
                while (cond != nullptr) {
                    pipeline* pipe = cond->pipeline_child;
                    while (pipe != nullptr) {
                        command* cmd = pipe->command_child;
                        while (cmd != nullptr) {
                            command* to_delete = cmd;
                            cmd = cmd->next_command;
                            delete to_delete;
                        }
                        pipeline* to_delete = pipe;
                        pipe = pipe->next_pipeline;
                        delete to_delete;
                    }
                    conditional* to_delete = cond;
                    cond = cond->next_conditional;
                    delete to_delete;
                }
            }
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes
        while (true) {
            int reaped = waitpid(-1, nullptr, WNOHANG);
            if (reaped <= 0) {
                break;
            }
        }

        // Handle interrupt requests
        if (interrupt_flag == 1) {
            printf("sh61[%d]$ ", getpid());
        }
    }

    return 0;
}
