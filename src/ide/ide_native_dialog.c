/*
 * ide_native_dialog.c — Windows native file open/save dialog + folder picker.
 *
 * Because windows.h clashes with raylib's Rectangle/CloseWindow/DrawText names,
 * this file is a SEPARATE translation unit. ide_main.c only uses an extern prototype.
 */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *gcl_ide_native_dialog(int save, const char *initial_dir) {
    OPENFILENAMEA ofn;
    char file[2048] = "";
    char dir[2048] = "";
    if (initial_dir) snprintf(dir, sizeof(dir), "%s", initial_dir);
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrInitialDir = dir[0] ? dir : NULL;
    ofn.lpstrFilter = "All Files\0*.*\0Text Files\0*.txt;*.gcsf;*.lua;*.c;*.h;*.md\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = save ? "Save File" : "Open File";
    ofn.lpstrDefExt = "txt";
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST)
                     : (OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST);

    BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (ok) {
        size_t n = strlen(file) + 1;
        char *r = (char *)malloc(n);
        if (r) memcpy(r, file, n);
        return r;
    }
    return NULL;
}

char *gcl_ide_native_dialog_folder(char *out, size_t outsz) {
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = "Select Project Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            snprintf(out, outsz, "%s", path);
            CoTaskMemFree(pidl);
            return out;
        }
        CoTaskMemFree(pidl);
    }
    return NULL;
}

#else

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Linux / WSL: no native file dialog. Zenity (GNOME) or kdialog (KDE) opens a
   real GTK/KDE dialog. If neither exists, a simple PATH-based file picker
   (ls + enter) is used. This way the "Open Project"/"Browse" buttons
   actually work on Linux too — in WSL, if zenity is installed it opens via X11. */

static char *run_capture(char *const argv[]) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }
    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            (void)dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    char *buf = NULL;
    size_t cap = 0, len = 0;
    char tmp[1024];
    int failed = 0;
    for (;;) {
        ssize_t got = read(pipefd[0], tmp, sizeof(tmp));
        if (got == 0) break;
        if (got < 0) {
            if (errno == EINTR) continue;
            failed = 1;
            break;
        }
        size_t t = (size_t)got;
        if (t > SIZE_MAX - len - 1) {
            failed = 1;
            break;
        }
        if (len + t + 1 > cap) {
            size_t next_cap = cap ? cap : 1024;
            while (next_cap < len + t + 1) {
                if (next_cap > SIZE_MAX / 2) {
                    next_cap = len + t + 1;
                    break;
                }
                next_cap *= 2;
            }
            char *nb = (char *)realloc(buf, next_cap);
            if (!nb) {
                failed = 1;
                break;
            }
            buf = nb;
            cap = next_cap;
        }
        memcpy(buf + len, tmp, t);
        len += t;
    }
    close(pipefd[0]);
    if (failed) (void)kill(pid, SIGTERM);
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            free(buf);
            return NULL;
        }
    }
    if (failed || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(buf);
        return NULL;
    }
    if (buf) buf[len] = '\0';
    /* trim the trailing newline in the output */
    if (buf) {
        size_t l = strlen(buf);
        while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r')) buf[--l] = '\0';
    }
    return buf;
}

static int has_cmd(const char *cmd) {
    if (!cmd || !cmd[0]) return 0;
    const char *path = getenv("PATH");
    if (!path) return 0;
    const char *part = path;
    for (;;) {
        const char *end = strchr(part, ':');
        size_t len = end ? (size_t)(end - part) : strlen(part);
        char candidate[1024];
        if (len + strlen(cmd) + 2 <= sizeof(candidate)) {
            if (len == 0) {
                snprintf(candidate, sizeof(candidate), "./%s", cmd);
            } else {
                memcpy(candidate, part, len);
                candidate[len] = '/';
                snprintf(candidate + len + 1, sizeof(candidate) - len - 1, "%s", cmd);
            }
            if (access(candidate, X_OK) == 0) return 1;
        }
        if (!end) break;
        part = end + 1;
    }
    return 0;
}

char *gcl_ide_native_dialog(int save, const char *initial_dir) {
    if (has_cmd("zenity")) {
        char filename_arg[4096];
        snprintf(filename_arg, sizeof(filename_arg), "--filename=%s/",
                 initial_dir ? initial_dir : ".");
        char *open_args[] = { "zenity", "--file-selection", filename_arg, NULL };
        char *save_args[] = { "zenity", "--file-selection", "--save", "--confirm-overwrite", filename_arg, NULL };
        char **args = save ? save_args : open_args;
        char *r = run_capture(args);
        if (r && r[0]) return r;
        free(r);
    }
    if (has_cmd("kdialog")) {
        char dirname[4096];
        snprintf(dirname, sizeof(dirname), "%s/", initial_dir ? initial_dir : ".");
        char *open_args[] = { "kdialog", "--getopenfilename", dirname, "*", NULL };
        char *save_args[] = { "kdialog", "--getsavefilename", dirname, "*", NULL };
        char *r = run_capture(save ? save_args : open_args);
        if (r && r[0]) return r;
        free(r);
    }
    /* Fallback: simple text file path picker using printf */
    fprintf(stderr, "No zenity/kdialog found. Enter a file path (or leave empty to cancel): ");
    fflush(stderr);
    char line[4096];
    if (fgets(line, sizeof(line), stdin)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (line[0]) {
            char *r = (char *)malloc(l + 1);
            if (r) memcpy(r, line, l + 1);
            return r;
        }
    }
    return NULL;
}

char *gcl_ide_native_dialog_folder(char *out, size_t outsz) {
    if (!out || outsz == 0) return NULL;
    if (has_cmd("zenity")) {
        char *args[] = { "zenity", "--file-selection", "--directory", NULL };
        char *r = run_capture(args);
        if (r && r[0]) {
            snprintf(out, outsz, "%s", r);
            free(r);
            return out;
        }
        free(r);
    }
    if (has_cmd("kdialog")) {
        char *args[] = { "kdialog", "--getexistingdirectory", ".", NULL };
        char *r = run_capture(args);
        if (r && r[0]) {
            snprintf(out, outsz, "%s", r);
            free(r);
            return out;
        }
        free(r);
    }
    fprintf(stderr, "No zenity/kdialog found. Enter a folder path (or leave empty to cancel): ");
    fflush(stderr);
    char line[4096];
    if (fgets(line, sizeof(line), stdin)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (line[0]) {
            snprintf(out, outsz, "%s", line);
            return out;
        }
    }
    return NULL;
}

#endif
